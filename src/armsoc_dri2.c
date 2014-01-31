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

#include <unistd.h>
#include "armsoc_driver.h"
#include "armsoc_exa.h"

#include "dri2.h"

/* any point to support earlier? */
#if DRI2INFOREC_VERSION < 4
#	error "Requires newer DRI2"
#endif

#include "drmmode_driver.h"

struct ARMSOCDRI2BufferRec {
	DRI2BufferRec base;

	/**
	 * The DRI2 buffers are reference counted to avoid crashyness when the
	 * client detaches a dri2 drawable while we are still waiting for a
	 * page_flip event.
	 */
	int refcnt;

	/**
	 * The value of canflip() for the previous frame. Used so that we can
	 * tell whether the buffer should be re-allocated, e.g into scanout-able
	 * memory if the buffer can now be flipped.
	 *
	 * We don't want to re-allocate every frame because it is unnecessary
	 * overhead most of the time apart from when we switch from flipping
	 * to blitting or vice versa.
	 *
	 * We should bump the serial number of the drawable if canflip() returns
	 * something different to what is stored here, so that the DRI2 buffers
	 * will get re-allocated.
	 */
	int previous_canflip;

	struct armsoc_bo *bo;
};

#define ARMSOCBUF(p)	((struct ARMSOCDRI2BufferRec *)(p))
#define DRIBUF(p)	((DRI2BufferPtr)(&(p)->base))


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

static inline void
exchangebufs(DrawablePtr pDraw, DRI2BufferPtr a, DRI2BufferPtr b)
{
	struct ARMSOCDRI2BufferRec *aa = ARMSOCBUF(a);
	struct ARMSOCDRI2BufferRec *bb = ARMSOCBUF(b);

	exchange(a->name, b->name);
	exchange(aa->bo, bb->bo);
}

/* Migrate pixmap to UMP buffer */
static struct armsoc_bo *
MigratePixmapToGEM(struct ARMSOCRec *pARMSOC, DrawablePtr pDraw)
{
    PixmapPtr pPixmap = (PixmapPtr) pDraw;
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    struct armsoc_bo *bo = armsoc_bo_from_drawable(pDraw);
    uint32_t pitch;
    unsigned char *addr;

    if (bo) {
        DEBUG_MSG("MigratePixmapToGEM %p, already exists = %p\n", pPixmap, bo);
	armsoc_bo_reference(bo);
        return bo;
    }

    /* create the GEM buffer */
    bo = armsoc_bo_new_with_dim(pARMSOC->dev,
                                pDraw->width,
                                pDraw->height,
                                pDraw->depth,
                                pDraw->bitsPerPixel,
				ARMSOC_BO_NON_SCANOUT);
    if (!bo) {
        ErrorF("MigratePixmapToGEM: bo alloc failed\n");
        return NULL;
    }

    addr = armsoc_bo_map(bo);
    pitch = armsoc_bo_pitch(bo);

    /* copy the pixel data to the new location */
    if (pitch == pPixmap->devKind) {
        memcpy(addr, pPixmap->devPrivate.ptr, armsoc_bo_size(bo));
    } else {
        int y;
        unsigned char *data = pPixmap->devPrivate.ptr;
        for (y = 0; y < pPixmap->drawable.height; y++) {
            memcpy(addr + y * pitch, 
                   data + y * pPixmap->devKind,
                   pPixmap->devKind);
        }
    }

    // FIXME Back these up?
    //umpbuf->BackupDevKind = pPixmap->devKind;
    //umpbuf->BackupDevPrivatePtr = pPixmap->devPrivate.ptr;

    pPixmap->devKind = pitch;
    pPixmap->devPrivate.ptr = addr;

    armsoc_bo_set_drawable(bo, pDraw);

    DEBUG_MSG("MigratePixmapToGEM %p, new buf = %p\n", pPixmap, bo);
    return bo;
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
	struct ARMSOCDRI2BufferRec *buf = calloc(1, sizeof(*buf));
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct armsoc_bo *bo;

	DEBUG_MSG("pDraw=%p, attachment=%d, format=%08x",
			pDraw, attachment, format);

	if (!buf) {
		ERROR_MSG("Couldn't allocate internal buffer structure");
		return NULL;
	}

	buf->refcnt = 1;
	buf->previous_canflip = canflip(pDraw);
	DRIBUF(buf)->attachment = attachment;
	DRIBUF(buf)->cpp = pDraw->bitsPerPixel / 8;
	DRIBUF(buf)->format = format + 1; /* suppress DRI2 buffer reuse */
	DRIBUF(buf)->flags = 0;

	/* If it is a pixmap, just migrate to a GEM buffer */
	if (pDraw->type == DRAWABLE_PIXMAP)
	{
	    if (!(bo = MigratePixmapToGEM(pARMSOC, pDraw))) {
	        ErrorF("ARMSOCDRI2CreateBuffer: MigratePixmapToUMP failed\n");
	        free(buf);
	        return NULL;
	    }
	    DRIBUF(buf)->pitch = armsoc_bo_pitch(bo);
	    DRIBUF(buf)->name = armsoc_bo_name(bo);
            buf->bo = bo;
	    return DRIBUF(buf);
	}

	/* We are not interested in anything other than back buffer requests ... */
	if (attachment != DRI2BufferBackLeft || pDraw->type != DRAWABLE_WINDOW) {
		/* ... and just return some dummy UMP buffer */
		bo = pARMSOC->scanout;
		DRIBUF(buf)->pitch = armsoc_bo_pitch(bo);
		DRIBUF(buf)->name = armsoc_bo_name(bo);
		buf->bo = bo;
		armsoc_bo_reference(bo);
		return DRIBUF(buf);
	}

	bo = armsoc_bo_from_drawable(pDraw);
	if (bo && armsoc_bo_width(bo) == pDraw->width && armsoc_bo_height(bo) == pDraw->height && armsoc_bo_bpp(bo) == pDraw->bitsPerPixel) {
		// Reuse existing
		DRIBUF(buf)->pitch = armsoc_bo_pitch(bo);
		DRIBUF(buf)->name = armsoc_bo_name(bo);
		buf->bo = bo;
		armsoc_bo_reference(bo);
		return DRIBUF(buf);
	}

	bo = armsoc_bo_new_with_dim(pARMSOC->dev,
                                pDraw->width,
                                pDraw->height,
                                pDraw->depth,
                                pDraw->bitsPerPixel,
				canflip(pDraw) ? ARMSOC_BO_SCANOUT : ARMSOC_BO_NON_SCANOUT);

	armsoc_bo_set_drawable(bo, pDraw);
	DRIBUF(buf)->name = armsoc_bo_name(bo);
	DRIBUF(buf)->pitch = armsoc_bo_pitch(bo);
	buf->bo = bo;

	if (canflip(pDraw) && attachment != DRI2BufferFrontLeft) {
		/* Create an fb around this buffer. This will fail and we will
		 * fall back to blitting if the display controller hardware
		 * cannot scan out this buffer (for example, if it doesn't
		 * support the format or there was insufficient scanout memory
		 * at buffer creation time). */
		int ret = armsoc_bo_add_fb(bo);
		if (ret) {
			WARNING_MSG(
					"Falling back to blitting a flippable window");
		}
	}

	/* Register Pixmap as having a buffer that can be accessed externally,
	 * so needs synchronised access */
	// FIXME ARMSOCRegisterExternalAccess(pPixmap);

	return DRIBUF(buf);
}

/**
 * Destroy Buffer
 */
static void
ARMSOCDRI2DestroyBuffer(DrawablePtr pDraw, DRI2BufferPtr buffer)
{
	struct ARMSOCDRI2BufferRec *buf = ARMSOCBUF(buffer);

	if (--buf->refcnt > 0)
		return;

	armsoc_bo_unreference(buf->bo);
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
	RegionPtr pCopyClip;
	GCPtr pGC;
        PixmapPtr pScratchPixmap;
        struct ARMSOCDRI2BufferRec *src = ARMSOCBUF(pSrcBuffer);

	DEBUG_MSG("pDraw=%p, pDstBuffer=%p pSrcBuffer=%p",
			pDraw, pDstBuffer, pSrcBuffer);

	pGC = GetScratchGC(pDraw->depth, pScreen);
	if (!pGC)
		return;

	pCopyClip = REGION_CREATE(pScreen, NULL, 0);
	RegionCopy(pCopyClip, pRegion);
	(*pGC->funcs->ChangeClip) (pGC, CT_REGION, pCopyClip, 0);
	ValidateGC(pDraw, pGC);

	/* If the dst is the framebuffer, and we had a way to
	 * schedule a deferred blit synchronized w/ vsync, that
	 * would be a nice thing to do utilize here to avoid
	 * tearing..  when we have sync object support for GEM
	 * buffers, I think we could do something more clever
	 * here.
	 */

        pScratchPixmap = GetScratchPixmapHeader(pScreen,
		armsoc_bo_width(src->bo), armsoc_bo_height(src->bo),
		armsoc_bo_depth(src->bo), armsoc_bo_bpp(src->bo) * 8,
		armsoc_bo_pitch(src->bo), armsoc_bo_map(src->bo));
		

	pGC->ops->CopyArea((DrawablePtr) pScratchPixmap, pDraw, pGC,
			0, 0, pDraw->width, pDraw->height, 0, 0);
	FreeScratchPixmapHeader(pScratchPixmap);
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
	int swapCount;
	int flags;
	void *data;
};

static const char * const swap_names[] = {
		[DRI2_EXCHANGE_COMPLETE] = "exchange",
		[DRI2_BLIT_COMPLETE] = "blit",
		[DRI2_FLIP_COMPLETE] = "flip,"
};

void
ARMSOCDRI2SwapComplete(struct ARMSOCDRISwapCmd *cmd)
{
	ScreenPtr pScreen = cmd->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
        struct ARMSOCDRI2BufferRec *src = ARMSOCBUF(cmd->pSrcBuffer);
        struct ARMSOCDRI2BufferRec *dst = ARMSOCBUF(cmd->pDstBuffer);
	DrawablePtr pDraw = NULL;
	int status;
	struct armsoc_bo *old_src_bo, *old_dst_bo;

	if (--cmd->swapCount > 0)
		return;

	/* Save the old source bo for unreference below */
	old_src_bo = src->bo;
	old_dst_bo = dst->bo;

	if ((cmd->flags & ARMSOC_SWAP_FAIL) == 0) {
		DEBUG_MSG("%s complete: %d -> %d", swap_names[cmd->type],
			cmd->pSrcBuffer->attachment,
			cmd->pDstBuffer->attachment);

		status = dixLookupDrawable(&pDraw, cmd->draw_id, serverClient,
				M_ANY, DixWriteAccess);

		if (status == Success) {
			if (cmd->type != DRI2_BLIT_COMPLETE &&
			   (cmd->flags & ARMSOC_SWAP_FAKE_FLIP) == 0) {
				assert(cmd->type == DRI2_FLIP_COMPLETE);
				exchangebufs(pDraw, cmd->pSrcBuffer,
							cmd->pDstBuffer);
			}

			DRI2SwapComplete(cmd->client, pDraw, 0, 0, 0, cmd->type,
					cmd->func, cmd->data);

			if (cmd->type != DRI2_BLIT_COMPLETE &&
			   (cmd->flags & ARMSOC_SWAP_FAKE_FLIP) == 0) {
				assert(cmd->type == DRI2_FLIP_COMPLETE);
				armsoc_bo_set_drawable(old_dst_bo, pDraw);
				set_scanout_bo(pScrn, old_src_bo);
			}
		}
	}

	/* drop extra refcnt we obtained prior to swap:
	 */
	ARMSOCDRI2DestroyBuffer(pDraw, cmd->pSrcBuffer);
	ARMSOCDRI2DestroyBuffer(pDraw, cmd->pDstBuffer);
	armsoc_bo_unreference(old_src_bo);
	armsoc_bo_unreference(old_dst_bo);
	pARMSOC->pending_flips--;

	free(cmd);
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
	struct ARMSOCDRI2BufferRec *src = ARMSOCBUF(pSrcBuffer);
	struct ARMSOCDRI2BufferRec *dst = ARMSOCBUF(pDstBuffer);
	struct ARMSOCDRISwapCmd *cmd = calloc(1, sizeof(*cmd));
	struct armsoc_bo *src_bo, *dst_bo;
	int src_fb_id, dst_fb_id;
	int new_canflip, ret, do_flip;

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

	DEBUG_MSG("%d -> %d", pSrcBuffer->attachment, pDstBuffer->attachment);

	/* obtain extra ref on buffers to avoid them going away while we await
	 * the page flip event:
	 */
	ARMSOCDRI2ReferenceBuffer(pSrcBuffer);
	ARMSOCDRI2ReferenceBuffer(pDstBuffer);
	pARMSOC->pending_flips++;

	src_bo = src->bo;
	dst_bo = dst->bo;

	src_fb_id = armsoc_bo_get_fb(src_bo);
	dst_fb_id = armsoc_bo_get_fb(dst_bo);

	new_canflip = canflip(pDraw);
	if ((src->previous_canflip != new_canflip) ||
	    (dst->previous_canflip != new_canflip)) {
		/* The drawable has transitioned between being flippable and
		 * non-flippable or vice versa. Bump the serial number to force
		 * the DRI2 buffers to be re-allocated during the next frame so
		 * that:
		 * - It is able to be scanned out
		 *        (if drawable is now flippable), or
		 * - It is not taking up possibly scarce scanout-able memory
		 *        (if drawable is now not flippable)
		 */

		PixmapPtr pPix = pScreen->GetWindowPixmap((WindowPtr)pDraw);
		pPix->drawable.serialNumber = NEXT_SERIAL_NUMBER;
	}

	src->previous_canflip = new_canflip;
	dst->previous_canflip = new_canflip;

	armsoc_bo_reference(src_bo);
	armsoc_bo_reference(dst_bo);

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
		DEBUG_MSG("can flip:  %d -> %d", src_fb_id, dst_fb_id);
		cmd->type = DRI2_FLIP_COMPLETE;
		/* TODO: MIDEGL-1461: Handle rollback if multiple CRTC flip is
		 * only partially successful
		 */
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

			if (cmd->swapCount == 0)
				ARMSOCDRI2SwapComplete(cmd);

			return FALSE;
		} else {
			if (ret == 0)
				cmd->flags |= ARMSOC_SWAP_FAKE_FLIP;
			else {
				int i;
				/* Hack to wait for vblank, as it seems like
				 * Mali reuses the old front buffer as soon
				 * as ScheduleSwap returns. */
				for (i = 0; i < 200; i++) usleep(1);
				// FIXME should we exchangebufs here instead of later?
			}

			if (pARMSOC->drmmode_interface->use_page_flip_events)
				cmd->swapCount = ret;
			else
				cmd->swapCount = 0;

			if (cmd->swapCount == 0)
				ARMSOCDRI2SwapComplete(cmd);
		}
	} else {
		/* fallback to blit: */
		BoxRec box = {
				.x1 = 0,
				.y1 = 0,
				.x2 = pDraw->width,
				.y2 = pDraw->height,
		};
		RegionRec region;
		RegionInit(&region, &box, 0);
		ARMSOCDRI2CopyRegion(pDraw, &region, pDstBuffer, pSrcBuffer);
		cmd->type = DRI2_BLIT_COMPLETE;
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
		.version         = 5,
		.fd              = pARMSOC->drmFD,
		.driverName      = "armsoc",
		.deviceName      = pARMSOC->deviceName,
		.CreateBuffer    = ARMSOCDRI2CreateBuffer,
		.DestroyBuffer   = ARMSOCDRI2DestroyBuffer,
		.CopyRegion      = ARMSOCDRI2CopyRegion,
		.ScheduleSwap    = ARMSOCDRI2ScheduleSwap,
		.ScheduleWaitMSC = ARMSOCDRI2ScheduleWaitMSC,
		.GetMSC          = ARMSOCDRI2GetMSC,
		.AuthMagic       = drmAuthMagic,
	};
	int minor = 1, major = 0;

	if (xf86LoaderCheckSymbol("DRI2Version"))
		DRI2Version(&major, &minor);

	if (minor < 1) {
		WARNING_MSG("DRI2 requires DRI2 module version 1.1.0 or later");
		return FALSE;
	}

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
}
