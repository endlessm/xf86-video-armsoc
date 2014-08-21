/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright © 2011 Texas Instruments, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <rob@ti.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "armsoc_driver.h"
#include "armsoc_exa.h"

#include "dri2.h"

/* any point to support earlier? */
#if DRI2INFOREC_VERSION < 5
#	error "Requires newer DRI2"
#endif

#include "drmmode_driver.h"

struct ARMSOCDRI2BufferRec {
	DRI2BufferRec base;

	/**
	 * Pixmap(s) that are backing the buffer
	 *
	 * We assume that a window's front buffer pixmap is never reallocated,
	 * and therefore that it is safe to use the pointer to it stored here.
	 */
	PixmapPtr *pPixmaps;

	/**
	 * Pixmap that corresponds to the DRI2BufferRec.name, so wraps
	 * the buffer that will be used for DRI2GetBuffers calls and the
	 * next DRI2SwapBuffers call.
	 *
	 * When using more than double buffering this (and the name) are updated
	 * after a swap, before the next DRI2GetBuffers call.
	 */
	unsigned currentPixmap;

	/**
	 * Number of Pixmaps to use.
	 *
	 * This allows the number of back buffers used to be reduced, for
	 * example when allocation fails. It cannot be changed to increase the
	 * number of buffers as we would overflow the pPixmaps array.
	 */
	unsigned numPixmaps;

	/**
	 * The DRI2 buffers are reference counted to avoid crashyness when the
	 * client detaches a dri2 drawable while we are still waiting for a
	 * page_flip event.
	 */
	int refcnt;

	/**
         * We don't want to overdo attempting fb allocation for mapped
         * scanout buffers, to behave nice under low memory conditions.
         * Instead we use this flag to attempt the allocation just once
         * every time the window is mapped.
         */
	int attempted_fb_alloc;

};

#define ARMSOCBUF(p)	((struct ARMSOCDRI2BufferRec *)(p))
#define DRIBUF(p)	((DRI2BufferPtr)(&(p)->base))


static inline DrawablePtr
dri2draw(DrawablePtr pDraw, DRI2BufferPtr buf)
{
	if (buf->attachment == DRI2BufferFrontLeft)
		return pDraw;
	else {
		const unsigned curPix = ARMSOCBUF(buf)->currentPixmap;
		return &(ARMSOCBUF(buf)->pPixmaps[curPix]->drawable);
	}
}

static Bool
canflip(DrawablePtr pDraw)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	if (pARMSOC->NoFlip) {
		/* flipping is disabled by user option */
		return FALSE;
	} else {
		return (pDraw->type == DRAWABLE_WINDOW) &&
				DRI2CanFlip(pDraw);
	}
}

static inline Bool
exchangebufs(DrawablePtr pDraw, DRI2BufferPtr a, DRI2BufferPtr b)
{
	PixmapPtr aPix = draw2pix(dri2draw(pDraw, a));
	PixmapPtr bPix = draw2pix(dri2draw(pDraw, b));

	ARMSOCPixmapExchange(aPix, bPix);
	exchange(a->name, b->name);
	return TRUE;
}

static PixmapPtr
createpix(DrawablePtr pDraw)
{
	ScreenPtr pScreen = pDraw->pScreen;
	int flags = canflip(pDraw) ? ARMSOC_CREATE_PIXMAP_SCANOUT : CREATE_PIXMAP_USAGE_BACKING_PIXMAP;
	return pScreen->CreatePixmap(pScreen,
			pDraw->width, pDraw->height, pDraw->depth, flags);
}

static inline Bool
canexchange(DrawablePtr pDraw, struct armsoc_bo *src_bo, struct armsoc_bo *dst_bo)
{
	Bool ret = FALSE;
	ScreenPtr pScreen = pDraw->pScreen;
	PixmapPtr pRootPixmap, pWindowPixmap;
	int src_fb_id, dst_fb_id;

	pRootPixmap = pScreen->GetWindowPixmap(pScreen->root);
	pWindowPixmap = pDraw->type == DRAWABLE_PIXMAP ? (PixmapPtr)pDraw : pScreen->GetWindowPixmap((WindowPtr)pDraw);

	src_fb_id = armsoc_bo_get_fb(src_bo);
	dst_fb_id = armsoc_bo_get_fb(dst_bo);

	if (pRootPixmap != pWindowPixmap &&
	    armsoc_bo_width(src_bo) == armsoc_bo_width(dst_bo) &&
	    armsoc_bo_height(src_bo) == armsoc_bo_height(dst_bo) &&
	    armsoc_bo_bpp(src_bo) == armsoc_bo_bpp(dst_bo) &&
	    armsoc_bo_width(src_bo) == pDraw->width &&
	    armsoc_bo_height(src_bo) == pDraw->height &&
	    armsoc_bo_bpp(src_bo) == pDraw->bitsPerPixel &&
	    src_fb_id == 0 && dst_fb_id == 0) {
		ret = TRUE;
	}

	return ret;
}

/**
 * Create Buffer.
 *
 * Note that 'format' is used from the client side to specify the DRI buffer
 * format, which could differ from the drawable format.  For example, the
 * drawable could be 32b RGB, but the DRI buffer some YUV format (video) or
 * perhaps lower bit depth RGB (GL).  The color conversion is handled when
 * blitting to front buffer, and page-flipping (overlay or flipchain) can
 * only be used if the display supports.
 */
static DRI2BufferPtr
ARMSOCDRI2CreateBuffer(DrawablePtr pDraw, unsigned int attachment,
		unsigned int format)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCDRI2BufferRec *buf;
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	PixmapPtr pPixmap = NULL;
	struct armsoc_bo *bo;
	int ret;

	DEBUG_MSG("pDraw=%p, attachment=%d, format=%08x",
			pDraw, attachment, format);

	buf = calloc(1, sizeof *buf);
	if (!buf) {
		ERROR_MSG("Couldn't allocate internal buffer structure");
		return NULL;
	}

	if (attachment == DRI2BufferFrontLeft) {
		pPixmap = draw2pix(pDraw);
		pPixmap->refcnt++;
	} else {
		pPixmap = createpix(pDraw);
	}

	if (!pPixmap) {
		assert(attachment != DRI2BufferFrontLeft);
		ERROR_MSG("Failed to create back buffer for window");
		goto fail;
	}

	if (attachment == DRI2BufferBackLeft && pARMSOC->driNumBufs > 2) {
		buf->pPixmaps = calloc(pARMSOC->driNumBufs-1,
				sizeof(PixmapPtr));
		buf->numPixmaps = pARMSOC->driNumBufs-1;
	} else {
		buf->pPixmaps = malloc(sizeof(PixmapPtr));
		buf->numPixmaps = 1;
	}

	if (!buf->pPixmaps) {
		ERROR_MSG("Failed to allocate PixmapPtr array for DRI2Buffer");
		goto fail;
	}

	buf->pPixmaps[0] = pPixmap;
	assert(buf->currentPixmap == 0);

	bo = ARMSOCPixmapBo(pPixmap);
	if (!bo) {
		ERROR_MSG(
				"Attempting to DRI2 wrap a pixmap with no DRM buffer object backing");
		goto fail;
	}

	DRIBUF(buf)->attachment = attachment;
	DRIBUF(buf)->pitch = exaGetPixmapPitch(pPixmap);
	DRIBUF(buf)->cpp = pPixmap->drawable.bitsPerPixel / 8;
	DRIBUF(buf)->format = format;
	DRIBUF(buf)->flags = 0;
	buf->refcnt = 1;

	ret = armsoc_bo_get_name(bo, &DRIBUF(buf)->name);
	if (ret) {
		ERROR_MSG("could not get buffer name: %d", ret);
		goto fail;
	}

	if (canflip(pDraw) && attachment != DRI2BufferFrontLeft) {
		/* Create an fb around this buffer. This will fail and we will
		 * fall back to blitting if the display controller hardware
		 * cannot scan out this buffer (for example, if it doesn't
		 * support the format or there was insufficient scanout memory
		 * at buffer creation time).
		 *
		 * If the window is not mapped at this time, we will not hit
		 * this codepath, but ARMSOCDRI2ReuseBufferNotify will create
		 * a framebuffer if it gets mapped later on. */
		int ret = armsoc_bo_add_fb(bo);
	        buf->attempted_fb_alloc = TRUE;
		if (ret) {
			WARNING_MSG(
					"Falling back to blitting a flippable window");
		}
#if DRI2INFOREC_VERSION >= 6
		else if (FALSE == DRI2SwapLimit(pDraw, pARMSOC->swap_chain_size)) {
			WARNING_MSG(
				"Failed to set DRI2SwapLimit(%x,%d)",
				(unsigned int)pDraw, pARMSOC->swap_chain_size);
		}
#endif /* DRI2INFOREC_VERSION >= 6 */
	} else {
	}

	/* Register Pixmap as having a buffer that can be accessed externally,
	 * so needs synchronised access */
	ARMSOCRegisterExternalAccess(pPixmap);

	return DRIBUF(buf);

fail:
	if (pPixmap != NULL) {
		if (attachment != DRI2BufferFrontLeft)
			pScreen->DestroyPixmap(pPixmap);
		else
			pPixmap->refcnt--;
	}
	free(buf->pPixmaps);
	free(buf);

	return NULL;
}

/* Called when DRI2 is handling a GetBuffers request and is going to
 * reuse a buffer that we created earlier.
 * Our interest in this situation is that we might have omitted creating
 * a framebuffer for a backbuffer due to it not being flippable at creation
 * time (e.g. because the window wasn't mapped yet).
 * But if GetBuffers has been called because the window is now mapped,
 * we are going to need a framebuffer so that we can page flip it later.
 * We avoid creating a framebuffer when it is not necessary in order to save
 * on scanout memory which is potentially scarce.
 *
 * Mali r4p0 is generally light on calling GetBuffers (e.g. it doesn't do it
 * in response to an InvalidateBuffers event) but we have determined
 * experimentally that it does always seem to call GetBuffers upon a
 * unmapped-to-mapped transition.
 */
static void
ARMSOCDRI2ReuseBufferNotify(DrawablePtr pDraw, DRI2BufferPtr buffer)
{
	struct ARMSOCDRI2BufferRec *buf = ARMSOCBUF(buffer);
	struct armsoc_bo *bo;
	Bool flippable;
	int fb_id;

	if (buffer->attachment == DRI2BufferFrontLeft)
		return;

	bo = ARMSOCPixmapBo(buf->pPixmaps[0]);
	fb_id = armsoc_bo_get_fb(bo);
	flippable = canflip(pDraw);

	/* Detect unflippable-to-flippable transition:
	 * Window is flippable, but we haven't yet tried to allocate a
	 * framebuffer for it, and it doesn't already have a framebuffer.
	 * This can happen when CreateBuffer was called before the window
	 * was mapped, and we have now been mapped. */
	if (flippable && !buf->attempted_fb_alloc && fb_id == 0) {
		armsoc_bo_add_fb(bo);
	        buf->attempted_fb_alloc = TRUE;
	}

	/* Detect flippable-to-unflippable transition:
	 * Window is now unflippable, but we have a framebuffer allocated for
	 * it. Now we can free the framebuffer to save on scanout memory, and
	 * reset state in case it gets mapped again later. */
	if (!flippable && fb_id != 0) {
	        buf->attempted_fb_alloc = FALSE;
		armsoc_bo_rm_fb(bo);
	}
}

/**
 * Destroy Buffer
 */
static void
ARMSOCDRI2DestroyBuffer(DrawablePtr pDraw, DRI2BufferPtr buffer)
{
	struct ARMSOCDRI2BufferRec *buf = ARMSOCBUF(buffer);
	/* Note: pDraw may already be deleted, so use the pPixmap here
	 * instead (since it is at least refcntd)
	 */
	ScreenPtr pScreen = buf->pPixmaps[0]->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	int numBuffers, i;

	if (--buf->refcnt > 0)
		return;

	DEBUG_MSG("pDraw=%p, DRIbuffer=%p", pDraw, buffer);

	if (buffer->attachment == DRI2BufferBackLeft) {
		assert(pARMSOC->driNumBufs > 1);
		numBuffers = pARMSOC->driNumBufs-1;
	} else
		numBuffers = 1;

	for (i = 0; i < numBuffers && buf->pPixmaps[i] != NULL; i++) {
		ARMSOCDeregisterExternalAccess(buf->pPixmaps[i]);
		pScreen->DestroyPixmap(buf->pPixmaps[i]);
	}

	free(buf->pPixmaps);
	free(buf);
}

static void
ARMSOCDRI2ReferenceBuffer(DRI2BufferPtr buffer)
{
	struct ARMSOCDRI2BufferRec *buf = ARMSOCBUF(buffer);

	buf->refcnt++;
}

/**
 *
 */
static void
ARMSOCDRI2CopyRegion(DrawablePtr pDraw, RegionPtr pRegion,
		DRI2BufferPtr pDstBuffer, DRI2BufferPtr pSrcBuffer)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	DrawablePtr pSrcDraw = dri2draw(pDraw, pSrcBuffer);
	DrawablePtr pDstDraw = dri2draw(pDraw, pDstBuffer);
	RegionPtr pCopyClip;
	GCPtr pGC;

	DEBUG_MSG("pDraw=%p, pDstBuffer=%p (%p), pSrcBuffer=%p (%p)",
			pDraw, pDstBuffer, pSrcDraw, pSrcBuffer, pDstDraw);
	pGC = GetScratchGC(pDstDraw->depth, pScreen);
	if (!pGC)
		return;

	pCopyClip = REGION_CREATE(pScreen, NULL, 0);
	RegionCopy(pCopyClip, pRegion);
	(*pGC->funcs->ChangeClip) (pGC, CT_REGION, pCopyClip, 0);
	ValidateGC(pDstDraw, pGC);

	/* If the dst is the framebuffer, and we had a way to
	 * schedule a deferred blit synchronized w/ vsync, that
	 * would be a nice thing to do utilize here to avoid
	 * tearing..  when we have sync object support for GEM
	 * buffers, I think we could do something more clever
	 * here.
	 */

	pGC->ops->CopyArea(pSrcDraw, pDstDraw, pGC,
			0, 0, pDraw->width, pDraw->height, 0, 0);

	FreeScratchGC(pGC);
}

/**
 * Get current frame count and frame count timestamp, based on drawable's
 * crtc.
 */
static int
ARMSOCDRI2GetMSC(DrawablePtr pDraw, CARD64 *ust, CARD64 *msc)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	drmVBlank vbl = { .request = {
		.type = DRM_VBLANK_RELATIVE,
		.sequence = 0,
	} };
	int ret;

	if (!pARMSOC->drmmode_interface->vblank_query_supported)
		return FALSE;

	ret = drmWaitVBlank(pARMSOC->drmFD, &vbl);
	if (ret) {
		ERROR_MSG("get vblank counter failed: %s", strerror(errno));
		return FALSE;
	}

	if (ust)
		*ust = ((CARD64)vbl.reply.tval_sec * 1000000)
			+ vbl.reply.tval_usec;

	if (msc)
		*msc = vbl.reply.sequence;

	return TRUE;
}

#if DRI2INFOREC_VERSION >= 6
/**
 * Called by DRI2 to validate that any new swap limit being set by
 * DRI2 is in range. In our case the range is 1 to the DRI2MaxBuffers
 * option, plus one in the case of early display usage.
 */
static Bool
ARMSOCDRI2SwapLimitValidate(DrawablePtr pDraw, int swap_limit) {
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	int32_t lower_limit, upper_limit;

	lower_limit = 1;
	upper_limit = pARMSOC->driNumBufs-1;

	if (pARMSOC->drmmode_interface->use_early_display)
		upper_limit += 1;

	return ((swap_limit >= lower_limit) && (swap_limit <= upper_limit))
		? TRUE : FALSE;
}
#endif /* DRI2INFOREC_VERSION >= 6 */

#define ARMSOC_SWAP_FAKE_FLIP (1 << 0)
#define ARMSOC_SWAP_FAIL      (1 << 1)

struct ARMSOCDRISwapCmd {
	int type;
	ClientPtr client;
	ScreenPtr pScreen;
	/* Note: store drawable ID, rather than drawable.  It's possible that
	 * the drawable can be destroyed while we wait for page flip event:
	 */
	XID draw_id;
	DRI2BufferPtr pDstBuffer;
	DRI2BufferPtr pSrcBuffer;
	DRI2SwapEventPtr func;
	int swapCount; /* number of crtcs with flips in flight for this swap */
	int flags;
	void *data;
	struct armsoc_bo *old_src_bo;  /* Swap chain holds ref on src bo */
	struct armsoc_bo *old_dst_bo;  /* Swap chain holds ref on dst bo */
	struct armsoc_bo *new_scanout; /* scanout to be used after swap */
	unsigned int swap_id;
};

static const char * const swap_names[] = {
		[DRI2_EXCHANGE_COMPLETE] = "exchange",
		[DRI2_BLIT_COMPLETE] = "blit",
		[DRI2_FLIP_COMPLETE] = "flip,"
};

static Bool allocNextBuffer(DrawablePtr pDraw, PixmapPtr *ppPixmap,
		uint32_t *name) {
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct armsoc_bo *bo;
	PixmapPtr pPixmap;
	int ret;
	uint32_t new_name;
	Bool extRegistered = FALSE;

	pPixmap = createpix(pDraw);

	if (!pPixmap)
		goto error;

	bo = ARMSOCPixmapBo(pPixmap);
	if (!bo) {
		WARNING_MSG(
			"Attempting to DRI2 wrap a pixmap with no DRM buffer object backing");
		goto error;
	}

	ARMSOCRegisterExternalAccess(pPixmap);
	extRegistered = TRUE;

	ret = armsoc_bo_get_name(bo, &new_name);
	if (ret) {
		ERROR_MSG("Could not get buffer name: %d", ret);
		goto error;
	}

	if (!armsoc_bo_get_fb(bo)) {
		ret = armsoc_bo_add_fb(bo);
		/* Should always be able to add fb, as we only add more buffers
		 * when flipping*/
		if (ret) {
			ERROR_MSG(
				"Could not add framebuffer to additional back buffer");
			goto error;
		}
	}

	/* No errors, update pixmap and name */
	*ppPixmap = pPixmap;
	*name = new_name;

	return TRUE;

error:
	/* revert to existing pixmap */
	if (pPixmap) {
		if (extRegistered)
			ARMSOCDeregisterExternalAccess(pPixmap);
		pScreen->DestroyPixmap(pPixmap);
	}

	return FALSE;
}

static void nextBuffer(DrawablePtr pDraw, struct ARMSOCDRI2BufferRec *backBuf)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	if (pARMSOC->driNumBufs <= 2) {
		/*Only using double buffering, leave the pixmap as-is */
		return;
	}

	backBuf->currentPixmap++;
	backBuf->currentPixmap %= backBuf->numPixmaps;

	if (backBuf->pPixmaps[backBuf->currentPixmap]) {
		/* Already allocated the next buffer - get the name and
		 * early-out */
		struct armsoc_bo *bo;
		int ret;

		bo = ARMSOCPixmapBo(backBuf->pPixmaps[backBuf->currentPixmap]);
		assert(bo);
		ret = armsoc_bo_get_name(bo, &DRIBUF(backBuf)->name);
		assert(!ret);
	} else {
		Bool ret;
		PixmapPtr * const curBackPix =
			&backBuf->pPixmaps[backBuf->currentPixmap];
		ret = allocNextBuffer(pDraw, curBackPix,
			&DRIBUF(backBuf)->name);
		if (!ret) {
			/* can't have failed on the first buffer */
			assert(backBuf->currentPixmap > 0);
			/* Fall back to last buffer */
			backBuf->currentPixmap--;
			WARNING_MSG(
				"Failed to use the requested %d-buffering due to an allocation failure.\n"
				"Falling back to %d-buffering for this DRI2Drawable",
				backBuf->numPixmaps+1,
				backBuf->currentPixmap+2);
			backBuf->numPixmaps = backBuf->currentPixmap+1;
		}
	}
}

static struct armsoc_bo *boFromBuffer(DRI2BufferPtr buf)
{
	PixmapPtr pPixmap;
	struct ARMSOCPixmapPrivRec *priv;

	pPixmap = ARMSOCBUF(buf)->pPixmaps[ARMSOCBUF(buf)->currentPixmap];
	priv = exaGetPixmapDriverPrivate(pPixmap);
	return priv->bo;
}


static
void updateResizedBuffer(ScrnInfoPtr pScrn, void *buffer,
		struct armsoc_bo *old_bo, struct armsoc_bo *resized_bo) {
	DRI2BufferPtr dri2buf = (DRI2BufferPtr)buffer;
	struct ARMSOCDRI2BufferRec *buf = ARMSOCBUF(dri2buf);
	int i;

	for (i = 0; i < buf->numPixmaps; i++) {
		if (buf->pPixmaps[i] != NULL) {
			struct ARMSOCPixmapPrivRec *priv = exaGetPixmapDriverPrivate(buf->pPixmaps[i]);
			if (old_bo == priv->bo) {
				int ret;

				/* Update the buffer name if this pixmap is current */
				if (i == buf->currentPixmap) {
					ret = armsoc_bo_get_name(resized_bo, &dri2buf->name);
					assert(!ret);
				}

				/* pixmap takes ref on resized bo */
				armsoc_bo_reference(resized_bo);
				/* replace the old_bo with the resized_bo */
				priv->bo = resized_bo;
				/* pixmap drops ref on old bo */
				armsoc_bo_unreference(old_bo);
			}
		}
	}
}

void
ARMSOCDRI2ResizeSwapChain(ScrnInfoPtr pScrn, struct armsoc_bo *old_bo,
		struct armsoc_bo *resized_bo)
{
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct ARMSOCDRISwapCmd *cmd = NULL;
	int i;
	int back = pARMSOC->swap_chain_count - 1; /* The last swap scheduled */

	/* Update the bos for each scheduled swap in the swap chain */
	for (i = 0; i < pARMSOC->swap_chain_size && back >= 0; i++) {
		unsigned int idx = back % pARMSOC->swap_chain_size;

		cmd = pARMSOC->swap_chain[idx];
		back--;
		if (!cmd)
			continue;
		updateResizedBuffer(pScrn, cmd->pSrcBuffer, old_bo, resized_bo);
		updateResizedBuffer(pScrn, cmd->pDstBuffer, old_bo, resized_bo);
	}
}


void
ARMSOCDRI2SwapComplete(struct ARMSOCDRISwapCmd *cmd)
{
	ScreenPtr pScreen = cmd->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	DrawablePtr pDraw = NULL;
	unsigned int idx = 0;
	int status;

	if (--cmd->swapCount > 0) /* wait for all crtcs to flip */
		return;

	if ((cmd->flags & ARMSOC_SWAP_FAIL) == 0) {
		DEBUG_MSG("swap %d %s complete: %d -> %d",
			cmd->swap_id, swap_names[cmd->type],
			cmd->pSrcBuffer->attachment,
			cmd->pDstBuffer->attachment);
		status = dixLookupDrawable(&pDraw, cmd->draw_id, serverClient,
				M_ANY, DixWriteAccess);

		if (status == Success) {

			DRI2SwapComplete(cmd->client, pDraw, 0, 0, 0, cmd->type,
					cmd->func, cmd->data);

			if (cmd->type != DRI2_BLIT_COMPLETE &&
			    cmd->type != DRI2_EXCHANGE_COMPLETE &&
			   (cmd->flags & ARMSOC_SWAP_FAKE_FLIP) == 0) {
				assert(cmd->type == DRI2_FLIP_COMPLETE);
				set_scanout_bo(pScrn, cmd->new_scanout);
			}
		} else {
			ERROR_MSG("dixLookupDrawable fail on swap complete");
		}
	} else {
		ERROR_MSG("swap %d ARMSOC_SWAP_FAIL on swap complete", cmd->swap_id);
	}

	/* drop extra refcnt we obtained prior to swap:
	 */
	ARMSOCDRI2DestroyBuffer(pDraw, cmd->pSrcBuffer);
	ARMSOCDRI2DestroyBuffer(pDraw, cmd->pDstBuffer);

	/* swap chain drops ref on original src bo */
	armsoc_bo_unreference(cmd->old_src_bo);
	/* swap chain drops ref on original dst bo */
	armsoc_bo_unreference(cmd->old_dst_bo);

	if (cmd->type == DRI2_FLIP_COMPLETE) {
		pARMSOC->pending_flips--;
		/* Free the swap cmd and remove it from the swap chain. */
		idx = cmd->swap_id % pARMSOC->swap_chain_size;
		assert(pARMSOC->swap_chain[idx] == cmd);
		pARMSOC->swap_chain[idx] = NULL;
	}
	free(cmd);
	pARMSOC->swap_chain[idx] = NULL;
}

/**
 * ScheduleSwap is responsible for requesting a DRM vblank event for the
 * appropriate frame.
 *
 * In the case of a blit (e.g. for a windowed swap) or buffer exchange,
 * the vblank requested can simply be the last queued swap frame + the swap
 * interval for the drawable.
 *
 * In the case of a page flip, we request an event for the last queued swap
 * frame + swap interval - 1, since we'll need to queue the flip for the frame
 * immediately following the received event.
 */
static int
ARMSOCDRI2ScheduleSwap(ClientPtr client, DrawablePtr pDraw,
		DRI2BufferPtr pDstBuffer, DRI2BufferPtr pSrcBuffer,
		CARD64 *target_msc, CARD64 divisor, CARD64 remainder,
		DRI2SwapEventPtr func, void *data)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct ARMSOCDRISwapCmd *cmd;
	struct armsoc_bo *src_bo, *dst_bo;
	int src_fb_id, dst_fb_id;
	int ret, do_flip;
	unsigned int idx;
	RegionRec region;
	PixmapPtr pDstPixmap = draw2pix(dri2draw(pDraw, pDstBuffer));

	cmd = calloc(1, sizeof(*cmd));
	if (!cmd)
		return FALSE;

	cmd->client = client;
	cmd->pScreen = pScreen;
	cmd->draw_id = pDraw->id;
	cmd->pSrcBuffer = pSrcBuffer;
	cmd->pDstBuffer = pDstBuffer;
	cmd->swapCount = 0;
	cmd->flags = 0;
	cmd->func = func;
	cmd->data = data;

	region.extents.x1 = region.extents.y1 = 0;
	region.extents.x2 = pDstPixmap->drawable.width;
	region.extents.y2 = pDstPixmap->drawable.height;
	region.data = NULL;
	DamageRegionAppend(&pDstPixmap->drawable, &region);
	DamageRegionProcessPending(&pDstPixmap->drawable);

	/* obtain extra ref on DRI buffers to avoid them going
	 * away while we await the page flip event.
	 */
	ARMSOCDRI2ReferenceBuffer(pSrcBuffer);
	ARMSOCDRI2ReferenceBuffer(pDstBuffer);

	src_bo = boFromBuffer(pSrcBuffer);
	dst_bo = boFromBuffer(pDstBuffer);

	src_fb_id = armsoc_bo_get_fb(src_bo);
	dst_fb_id = armsoc_bo_get_fb(dst_bo);

	/* Store and reference actual buffer-objects used in case
	 * the pixmaps dissapear.
	 */
	cmd->old_src_bo = src_bo;
	cmd->old_dst_bo = dst_bo;

	/* Swap chain takes a ref on original src bo */
	armsoc_bo_reference(cmd->old_src_bo);
	/* Swap chain takes a ref on original dst bo */
	armsoc_bo_reference(cmd->old_dst_bo);

	DEBUG_MSG("SWAP %d SCHEDULED : %d -> %d ", cmd->swap_id,
				pSrcBuffer->attachment, pDstBuffer->attachment);

	do_flip = src_fb_id && dst_fb_id && canflip(pDraw);

	/* After a resolution change the back buffer (src) will still be
	 * of the original size. We can't sensibly flip to a framebuffer of
	 * a different size to the current resolution (it will look corrupted)
	 * so we must do a copy for this frame (which will clip the contents
	 * as expected).
	 *
	 * Once the client calls DRI2GetBuffers again, it will receive a new
	 * back buffer of the same size as the new resolution, and subsequent
	 * DRI2SwapBuffers will result in a flip.
	 */
	do_flip = do_flip &&
			(armsoc_bo_width(src_bo) == armsoc_bo_width(dst_bo));
	do_flip = do_flip &&
			(armsoc_bo_height(src_bo) == armsoc_bo_height(dst_bo));

	if (do_flip) {
		DEBUG_MSG("FLIPPING:  FB%d -> FB%d", src_fb_id, dst_fb_id);
		cmd->type = DRI2_FLIP_COMPLETE;

		/* Add swap operation to the swap chain */
		cmd->swap_id = pARMSOC->swap_chain_count++;
		idx = cmd->swap_id % pARMSOC->swap_chain_size;
		assert(NULL == pARMSOC->swap_chain[idx]);
		pARMSOC->swap_chain[idx] = cmd;
		/* TODO: MIDEGL-1461: Handle rollback if multiple CRTC flip is
		 * only partially successful
		 */
		pARMSOC->pending_flips++;
		ret = drmmode_page_flip(pDraw, src_fb_id, cmd);

		/* If using page flip events, we'll trigger an immediate
		 * completion in the case that no CRTCs were enabled to be
		 * flipped. If not using page flip events, trigger immediate
		 * completion unconditionally.
		 */
		if (ret < 0) {
			/*
			 * Error while flipping; bail.
			 */
			cmd->flags |= ARMSOC_SWAP_FAIL;

			if (pARMSOC->drmmode_interface->use_page_flip_events)
				cmd->swapCount = -(ret + 1);
			else
				cmd->swapCount = 0;

			cmd->new_scanout = boFromBuffer(pDstBuffer);
			if (cmd->swapCount == 0)
				ARMSOCDRI2SwapComplete(cmd);

			return FALSE;
		} else {
			if (ret == 0)
				cmd->flags |= ARMSOC_SWAP_FAKE_FLIP;

			if (pARMSOC->drmmode_interface->use_page_flip_events)
				cmd->swapCount = ret;
			else
				cmd->swapCount = 0;

			/* Flip successfully scheduled.
			 * Now exchange bos between src and dst pixmaps
			 * and select the next bo for the back buffer.
			 */
			if (ret) {
				assert(cmd->type == DRI2_FLIP_COMPLETE);
				exchangebufs(pDraw, pSrcBuffer, pDstBuffer);

				if (pSrcBuffer->attachment == DRI2BufferBackLeft)
					nextBuffer(pDraw, ARMSOCBUF(pSrcBuffer));
			}

			/* Store the new scanout bo now as the destination
			 * buffer bo might be exchanged if another swap is
			 * scheduled before this swap completes
			 */
			cmd->new_scanout = boFromBuffer(pDstBuffer);
			if (cmd->swapCount == 0)
				ARMSOCDRI2SwapComplete(cmd);
		}
	} else if (canexchange(pDraw, src_bo, dst_bo)) {
		exchangebufs(pDraw, pSrcBuffer, pDstBuffer);
		if (pSrcBuffer->attachment == DRI2BufferBackLeft)
			nextBuffer(pDraw, ARMSOCBUF(pSrcBuffer));

		cmd->type = DRI2_EXCHANGE_COMPLETE;
		ARMSOCDRI2SwapComplete(cmd);
	} else {
		/* fallback to blit: */
		BoxRec box = {
				.x1 = 0,
				.y1 = 0,
				.x2 = pDraw->width,
				.y2 = pDraw->height,
		};
		RegionRec region;

		DEBUG_MSG("BLITTING");
		RegionInit(&region, &box, 0);
		ARMSOCDRI2CopyRegion(pDraw, &region, pDstBuffer, pSrcBuffer);
		cmd->type = DRI2_BLIT_COMPLETE;
		cmd->new_scanout = boFromBuffer(pDstBuffer);
		ARMSOCDRI2SwapComplete(cmd);
	}

	return TRUE;
}

/**
 * Request a DRM event when the requested conditions will be satisfied.
 *
 * We need to handle the event and ask the server to wake up the client when
 * we receive it.
 */
static int
ARMSOCDRI2ScheduleWaitMSC(ClientPtr client, DrawablePtr pDraw,
	CARD64 target_msc, CARD64 divisor, CARD64 remainder)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

	ERROR_MSG("not implemented");
	return FALSE;
}

/**
 * The DRI2 ScreenInit() function.. register our handler fxns w/ DRI2 core
 */
Bool
ARMSOCDRI2ScreenInit(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	DRI2InfoRec info = {
#if DRI2INFOREC_VERSION >= 6
		.version           = 6,
#else
		.version           = 5,
#endif
		.fd              = pARMSOC->drmFD,
		.driverName      = "armsoc",
		.deviceName      = pARMSOC->deviceName,
		.CreateBuffer    = ARMSOCDRI2CreateBuffer,
		.DestroyBuffer   = ARMSOCDRI2DestroyBuffer,
		.ReuseBufferNotify = ARMSOCDRI2ReuseBufferNotify,
		.CopyRegion      = ARMSOCDRI2CopyRegion,
		.ScheduleSwap    = ARMSOCDRI2ScheduleSwap,
		.ScheduleWaitMSC = ARMSOCDRI2ScheduleWaitMSC,
		.GetMSC          = ARMSOCDRI2GetMSC,
		.AuthMagic       = drmAuthMagic,
#if DRI2INFOREC_VERSION >= 6
		.SwapLimitValidate = ARMSOCDRI2SwapLimitValidate,
#endif
	};
	int minor = 1, major = 0;

	if (xf86LoaderCheckSymbol("DRI2Version"))
		DRI2Version(&major, &minor);

	if (minor < 1) {
		WARNING_MSG("DRI2 requires DRI2 module version 1.1.0 or later");
		return FALSE;
	}

	/* There is a one-to-one mapping with the DRI2SwapLimit
	 * feature and the swap chain size. If DRI2SwapLimit is
	 * not supported swap-chain will be of size 1.
	 */
	pARMSOC->swap_chain_size = 1;
	pARMSOC->swap_chain_count = 0;

	if (FALSE == pARMSOC->NoFlip &&
		pARMSOC->drmmode_interface->use_page_flip_events) {
#if DRI2INFOREC_VERSION < 6
		if (pARMSOC->drmmode_interface->use_early_display ||
				pARMSOC->driNumBufs > 2)
			ERROR_MSG("DRI2SwapLimit not supported, but buffers requested are > 2");
		else
			WARNING_MSG("DRI2SwapLimit not supported.");
#else
		/* Swap chain size or the swap limit must be set to one
		 * less than the number of buffers available unless we
		 * have early display enabled which uses one extra flip.
		 */
		if (pARMSOC->drmmode_interface->use_early_display)
			pARMSOC->swap_chain_size = pARMSOC->driNumBufs;
		else
			pARMSOC->swap_chain_size = pARMSOC->driNumBufs-1;
#endif
	}
	pARMSOC->swap_chain = calloc(pARMSOC->swap_chain_size,
		sizeof(*pARMSOC->swap_chain));

	INFO_MSG("Setting swap chain size: %d ", pARMSOC->swap_chain_size);

	return DRI2ScreenInit(pScreen, &info);
}

/**
 * The DRI2 CloseScreen() function.. unregister ourself w/ DRI2 core.
 */
void
ARMSOCDRI2CloseScreen(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	while (pARMSOC->pending_flips > 0) {
		DEBUG_MSG("waiting..");
		drmmode_wait_for_event(pScrn);
	}
	DRI2CloseScreen(pScreen);

	if (pARMSOC->swap_chain) {
		unsigned int idx = pARMSOC->swap_chain_count % pARMSOC->swap_chain_size;
		assert(!pARMSOC->swap_chain[idx]);
		free(pARMSOC->swap_chain);
		pARMSOC->swap_chain = NULL;
	}
}
