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
#if DRI2INFOREC_VERSION < 4
#	error "Requires newer DRI2"
#endif

#include "drmmode_driver.h"

typedef struct {
	DRI2BufferRec base;

	/**
	 * Pixmap that is backing the buffer
	 *
	 * NOTE: don't track the pixmap ptr for the front buffer if it is
	 * a window.. this could get reallocated from beneath us, so we should
	 * always use draw2pix to be sure to have the correct one
	 */
	PixmapPtr pPixmap;

	/**
	 * The DRI2 buffers are reference counted to avoid crashyness when the
	 * client detaches a dri2 drawable while we are still waiting for a
	 * page_flip event.
	 */
	int refcnt;

	/**
	 * The value of canflip() for the previous frame. Used so that we can tell
	 * whether the buffer should be re-allocated, e.g into scanout-able
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

} ARMSOCDRI2BufferRec, *ARMSOCDRI2BufferPtr;

#define ARMSOCBUF(p)	((ARMSOCDRI2BufferPtr)(p))
#define DRIBUF(p)	((DRI2BufferPtr)(&(p)->base))


static inline DrawablePtr
dri2draw(DrawablePtr pDraw, DRI2BufferPtr buf)
{
	if (buf->attachment == DRI2BufferFrontLeft) {
		return pDraw;
	} else {
		return &(ARMSOCBUF(buf)->pPixmap->drawable);
	}
}

static Bool
canflip(DrawablePtr pDraw)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);

	if( pARMSOC->NoFlip )	{
		/* flipping is disabled by user option */
		return FALSE;
	} else 	{
		return (pDraw->type == DRAWABLE_WINDOW) &&
				DRI2CanFlip(pDraw);
	}
}

static inline Bool
exchangebufs(DrawablePtr pDraw, DRI2BufferPtr a, DRI2BufferPtr b)
{
	PixmapPtr aPix = draw2pix(dri2draw(pDraw, a));
	PixmapPtr bPix = draw2pix(dri2draw(pDraw, b));

	ARMSOCPixmapExchange(aPix,bPix);
	exchange(a->name, b->name);
	return TRUE;
}

static PixmapPtr
createpix(DrawablePtr pDraw)
{
	ScreenPtr pScreen = pDraw->pScreen;
	int flags = canflip(pDraw) ? ARMSOC_CREATE_PIXMAP_SCANOUT : 0;
	return pScreen->CreatePixmap(pScreen,
			pDraw->width, pDraw->height, pDraw->depth, flags);
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
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	ARMSOCDRI2BufferPtr buf = calloc(1, sizeof(*buf));
	PixmapPtr pPixmap = NULL;
	struct armsoc_bo *bo;
	int ret;

	DEBUG_MSG("pDraw=%p, attachment=%d, format=%08x",
			pDraw, attachment, format);

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

	if (!pPixmap)
	{
		assert(attachment != DRI2BufferFrontLeft);
		ERROR_MSG("Failed to create back buffer for window");
		goto fail;
	}

	bo = ARMSOCPixmapBo(pPixmap);
	if (!bo)
	{
		ERROR_MSG("Attempting to DRI2 wrap a pixmap with no DRM buffer object backing");
		goto fail;
	}

	DRIBUF(buf)->attachment = attachment;
	DRIBUF(buf)->pitch = exaGetPixmapPitch(pPixmap);
	DRIBUF(buf)->cpp = pPixmap->drawable.bitsPerPixel / 8;
	DRIBUF(buf)->format = format;
	DRIBUF(buf)->flags = 0;
	buf->refcnt = 1;
	buf->pPixmap = pPixmap;
	buf->previous_canflip = -1;

	ret = armsoc_bo_get_name(bo, &DRIBUF(buf)->name);
	if (ret) {
		ERROR_MSG("could not get buffer name: %d", ret);
		goto fail;
	}

	/* Q: how to know across ARMSOC generations what formats that the display
	 * can support directly?
	 * A: attempt to create a drm_framebuffer, and if that fails then the
	 * hw must not support.. then fall back to blitting
	 */
	if (canflip(pDraw) && attachment != DRI2BufferFrontLeft) {
		int ret = armsoc_bo_add_fb(bo);
		if (ret) {
			/* to-bad, so-sad, we can't flip */
			WARNING_MSG("could not create fb: %d", ret);
		}
	}

	/* Register Pixmap as having a buffer that can be accessed externally,
	 * so needs synchronised access */
	ARMSOCRegisterExternalAccess(pPixmap);

	return DRIBUF(buf);

fail:
	if (pPixmap != NULL)
	{
		if (attachment != DRI2BufferFrontLeft)
		{
			pScreen->DestroyPixmap(pPixmap);
		}
		else
		{
			pPixmap->refcnt--;
		}
	}
	free(buf);

	return NULL;
}

/**
 * Destroy Buffer
 */
static void
ARMSOCDRI2DestroyBuffer(DrawablePtr pDraw, DRI2BufferPtr buffer)
{
	ARMSOCDRI2BufferPtr buf = ARMSOCBUF(buffer);
	/* Note: pDraw may already be deleted, so use the pPixmap here
	 * instead (since it is at least refcntd)
	 */
	ScreenPtr pScreen = buf->pPixmap->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

	if (--buf->refcnt > 0)
		return;

	DEBUG_MSG("pDraw=%p, buffer=%p", pDraw, buffer);

	ARMSOCDeregisterExternalAccess(buf->pPixmap);

	pScreen->DestroyPixmap(buf->pPixmap);

	free(buf);
}

static void
ARMSOCDRI2ReferenceBuffer(DRI2BufferPtr buffer)
{
	ARMSOCDRI2BufferPtr buf = ARMSOCBUF(buffer);
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
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	DrawablePtr pSrcDraw = dri2draw(pDraw, pSrcBuffer);
	DrawablePtr pDstDraw = dri2draw(pDraw, pDstBuffer);
	RegionPtr pCopyClip;
	GCPtr pGC;

	DEBUG_MSG("pDraw=%p, pDstBuffer=%p (%p), pSrcBuffer=%p (%p)",
			pDraw, pDstBuffer, pSrcDraw, pSrcBuffer, pDstDraw);

	pGC = GetScratchGC(pDstDraw->depth, pScreen);
	if (!pGC) {
		return;
	}

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
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);
	drmVBlank vbl = { .request = {
		.type = DRM_VBLANK_RELATIVE,
		.sequence = 0,
	} };
	int ret;

	ret = drmWaitVBlank(pARMSOC->drmFD, &vbl);
	if (ret) {
		static int limit = 5;
		if (limit) {
			ERROR_MSG("get vblank counter failed: %s", strerror(errno));
			limit--;
		}
		return FALSE;
	}

	if (ust) {
		*ust = ((CARD64)vbl.reply.tval_sec * 1000000) + vbl.reply.tval_usec;
	}
	if (msc) {
		*msc = vbl.reply.sequence;
	}

	return TRUE;
}

#define ARMSOC_SWAP_FAKE_FLIP (1 << 0)
#define ARMSOC_SWAP_FAIL      (1 << 1)

struct _ARMSOCDRISwapCmd {
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

static const char *swap_names[] = {
		[DRI2_EXCHANGE_COMPLETE] = "exchange",
		[DRI2_BLIT_COMPLETE] = "blit",
		[DRI2_FLIP_COMPLETE] = "flip,"
};

void
ARMSOCDRI2SwapComplete(ARMSOCDRISwapCmd *cmd)
{
	ScreenPtr pScreen = cmd->pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);
	DrawablePtr pDraw = NULL;
	int status;
	ARMSOCPixmapPrivPtr src_priv, dst_priv;
	struct armsoc_bo *old_src_bo, *old_dst_bo;

	if (--cmd->swapCount > 0)
		return;

	/* Save the old source bo for unreference below */
	src_priv = exaGetPixmapDriverPrivate(ARMSOCBUF(cmd->pSrcBuffer)->pPixmap);
	dst_priv = exaGetPixmapDriverPrivate(ARMSOCBUF(cmd->pDstBuffer)->pPixmap);
	old_src_bo = src_priv->bo;
	old_dst_bo = dst_priv->bo;

	if ((cmd->flags & ARMSOC_SWAP_FAIL) == 0) {
		DEBUG_MSG("%s complete: %d -> %d", swap_names[cmd->type],
				cmd->pSrcBuffer->attachment, cmd->pDstBuffer->attachment);

		status = dixLookupDrawable(&pDraw, cmd->draw_id, serverClient,
				M_ANY, DixWriteAccess);

		if (status == Success) {
			if (cmd->type != DRI2_BLIT_COMPLETE && (cmd->flags & ARMSOC_SWAP_FAKE_FLIP) == 0) {
				assert(cmd->type == DRI2_FLIP_COMPLETE);
				exchangebufs(pDraw, cmd->pSrcBuffer, cmd->pDstBuffer);
			}

			DRI2SwapComplete(cmd->client, pDraw, 0, 0, 0, cmd->type,
					cmd->func, cmd->data);

			if (cmd->type != DRI2_BLIT_COMPLETE && (cmd->flags & ARMSOC_SWAP_FAKE_FLIP) == 0) {
				dst_priv = exaGetPixmapDriverPrivate(draw2pix(dri2draw(pDraw, cmd->pDstBuffer)));
				assert(cmd->type == DRI2_FLIP_COMPLETE);
				set_scanout_bo(pScrn, dst_priv->bo);
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
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);
	ARMSOCDRI2BufferPtr src = ARMSOCBUF(pSrcBuffer);
	ARMSOCDRI2BufferPtr dst = ARMSOCBUF(pDstBuffer);
	ARMSOCDRISwapCmd *cmd = calloc(1, sizeof(*cmd));
	int src_fb_id, dst_fb_id;
	ARMSOCPixmapPrivPtr src_priv, dst_priv;
	int new_canflip, ret;

	if(!cmd)
	{
		return FALSE;
	}

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

	src_priv = exaGetPixmapDriverPrivate(src->pPixmap);
	dst_priv = exaGetPixmapDriverPrivate(dst->pPixmap);

	src_fb_id = armsoc_bo_get_fb(src_priv->bo);
	dst_fb_id = armsoc_bo_get_fb(dst_priv->bo);

	new_canflip = canflip(pDraw);
	if ((src->previous_canflip != -1 && src->previous_canflip != new_canflip) ||
	    (dst->previous_canflip != -1 && dst->previous_canflip != new_canflip) ||
	    (pARMSOC->has_resized))
	{
		/* The drawable has transitioned between being flippable and non-flippable
		 * or vice versa. Bump the serial number to force the DRI2 buffers to be
		 * re-allocated during the next frame so that:
		 * - It is able to be scanned out (if drawable is now flippable), or
		 * - It is not taking up possibly scarce scanout-able memory (if drawable
		 * is now not flippable)
		 *
		 * has_resized: On hotplugging back buffer needs to be invalidates as well
		 * as Xsever invalidates only the front buffer.
		 */

		PixmapPtr pPix = pScreen->GetWindowPixmap((WindowPtr)pDraw);
		pPix->drawable.serialNumber = NEXT_SERIAL_NUMBER;
	}

	src->previous_canflip = new_canflip;
	dst->previous_canflip = new_canflip;

	armsoc_bo_reference(src_priv->bo);
	armsoc_bo_reference(dst_priv->bo);
	if (src_fb_id && dst_fb_id && canflip(pDraw) && !(pARMSOC->has_resized)) {
		/* has_resized: On hotplug the fb size and crtc sizes arent updated
		* hence on this event we do a copyb but flip from the next frame
		* when the sizes are updated.
		*/
		DEBUG_MSG("can flip:  %d -> %d", src_fb_id, dst_fb_id);
		cmd->type = DRI2_FLIP_COMPLETE;
		/* TODO: MIDEGL-1461: Handle rollback if multiple CRTC flip is only partially successful
		 */
		ret = drmmode_page_flip(pDraw, src_fb_id, cmd);

		/* If using page flip events, we'll trigger an immediate completion in
		 * the case that no CRTCs were enabled to be flipped.  If not using page
		 * flip events, trigger immediate completion unconditionally.
		 */
		if (ret < 0) {
			/*
			 * Error while flipping; bail.
			 */
			cmd->flags |= ARMSOC_SWAP_FAIL;

			if (pARMSOC->drmmode->use_page_flip_events)
				cmd->swapCount = -(ret + 1);
			else
				cmd->swapCount = 0;

			if (cmd->swapCount == 0)
			{
				ARMSOCDRI2SwapComplete(cmd);
			}
			return FALSE;
		} else {
			if (ret == 0)
				cmd->flags |= ARMSOC_SWAP_FAKE_FLIP;

			if (pARMSOC->drmmode->use_page_flip_events)
				cmd->swapCount = ret;
			else
				cmd->swapCount = 0;

			if (cmd->swapCount == 0)
			{
				ARMSOCDRI2SwapComplete(cmd);
			}
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
		pARMSOC->has_resized = FALSE;
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
ARMSOCDRI2ScheduleWaitMSC(ClientPtr client, DrawablePtr pDraw, CARD64 target_msc,
		CARD64 divisor, CARD64 remainder)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

	ERROR_MSG("not implemented");
	return FALSE;
}

/**
 * The DRI2 ScreenInit() function.. register our handler fxns w/ DRI2 core
 */
Bool
ARMSOCDRI2ScreenInit(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);
	DRI2InfoRec info = {
			.version			= 5,
			.fd 				= pARMSOC->drmFD,
			.driverName			= "armsoc",
			.deviceName			= pARMSOC->deviceName,
			.CreateBuffer		= ARMSOCDRI2CreateBuffer,
			.DestroyBuffer		= ARMSOCDRI2DestroyBuffer,
			.CopyRegion			= ARMSOCDRI2CopyRegion,
			.ScheduleSwap		= ARMSOCDRI2ScheduleSwap,
			.ScheduleWaitMSC	= ARMSOCDRI2ScheduleWaitMSC,
			.GetMSC				= ARMSOCDRI2GetMSC,
			.AuthMagic			= drmAuthMagic,
	};
	int minor = 1, major = 0;

	if (xf86LoaderCheckSymbol("DRI2Version")) {
		DRI2Version(&major, &minor);
	}

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
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);
	while (pARMSOC->pending_flips > 0) {
		DEBUG_MSG("waiting..");
		drmmode_wait_for_event(pScrn);
	}
	DRI2CloseScreen(pScreen);
}