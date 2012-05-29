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

#include "omap_exa.h"
#include "omap_driver.h"

/* keep this here, instead of static-inline so submodule doesn't
 * need to know layout of OMAPPtr..
 */
_X_EXPORT OMAPEXAPtr
OMAPEXAPTR(ScrnInfoPtr pScrn)
{
	OMAPPtr pOMAP = OMAPPTR(pScrn);
	return pOMAP->pOMAPEXA;
}

/* Common OMAP EXA functions, mostly related to pixmap/buffer allocation.
 * Individual driver submodules can use these directly, or wrap them with
 * there own functions if anything additional is required.  Submodules
 * can use OMAPPrixmapPrivPtr#priv for their own private data.
 */

/* used by DRI2 code to play buffer switcharoo */
void
OMAPPixmapExchange(PixmapPtr a, PixmapPtr b)
{
	OMAPPixmapPrivPtr apriv = exaGetPixmapDriverPrivate(a);
	OMAPPixmapPrivPtr bpriv = exaGetPixmapDriverPrivate(b);
	exchange(apriv->priv, bpriv->priv);
	exchange(apriv->bo, bpriv->bo);
}

_X_EXPORT void *
OMAPCreatePixmap (ScreenPtr pScreen, int width, int height,
		int depth, int usage_hint, int bitsPerPixel,
		int *new_fb_pitch)
{
	OMAPPixmapPrivPtr priv = calloc(sizeof(OMAPPixmapPrivRec), 1);

	/* actual allocation of buffer is in OMAPModifyPixmapHeader */

	return priv;
}

_X_EXPORT void
OMAPDestroyPixmap(ScreenPtr pScreen, void *driverPriv)
{
	OMAPPixmapPrivPtr priv = driverPriv;

	omap_bo_del(priv->bo);

	free(priv);
}

_X_EXPORT Bool
OMAPModifyPixmapHeader(PixmapPtr pPixmap, int width, int height,
		int depth, int bitsPerPixel, int devKind,
		pointer pPixData)
{
	OMAPPixmapPrivPtr priv = exaGetPixmapDriverPrivate(pPixmap);
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	OMAPPtr pOMAP = OMAPPTR(pScrn);
	uint32_t flags = OMAP_BO_WC;
	Bool ret;

	ret = miModifyPixmapHeader(pPixmap, width, height, depth,
			bitsPerPixel, devKind, pPixData);
	if (!ret) {
		return ret;
	}

	if (pPixData == omap_bo_map(pOMAP->scanout)) {
		DEBUG_MSG("wrapping scanout buffer");
		priv->bo = pOMAP->scanout;
		return TRUE;
	} else if (pPixData) {
		/* we can't accelerate this pixmap, and don't ever want to
		 * see it again..
		 */
		pPixmap->devPrivate.ptr = pPixData;
		pPixmap->devKind = devKind;

		/* scratch-pixmap (see GetScratchPixmapHeader()) gets recycled,
		 * so could have a previous bo!
		 */
		omap_bo_del(priv->bo);
		priv->bo = NULL;

		return FALSE;
	}

	/* passed in values could be zero, indicating that existing values
	 * should be kept.. miModifyPixmapHeader() will deal with that, but
	 * we need to resync to ensure we have the right values in the rest
	 * of this function
	 */
	width	= pPixmap->drawable.width;
	height	= pPixmap->drawable.height;
	depth	= pPixmap->drawable.depth;
	bitsPerPixel = pPixmap->drawable.bitsPerPixel;

	if (pPixmap->usage_hint & OMAP_CREATE_PIXMAP_SCANOUT) {
		flags |= OMAP_BO_SCANOUT;
	}

	if (pPixmap->usage_hint & OMAP_CREATE_PIXMAP_TILED) {
		switch (bitsPerPixel) {
		case 8:
			flags |= OMAP_BO_TILED_8;
			break;
		case 16:
			flags |= OMAP_BO_TILED_16;
			break;
		case 32:
			flags |= OMAP_BO_TILED_32;
			break;
		default:
			break;
		}
	}

	if ((!priv->bo) || (omap_bo_width(priv->bo) != width)
	                || (omap_bo_height(priv->bo) != height)
	                || (omap_bo_bpp(priv->bo) != bitsPerPixel)) {
		/* re-allocate buffer! */
		omap_bo_del(priv->bo);
		if (flags & OMAP_BO_TILED) {
			priv->bo = omap_bo_new_tiled(pOMAP->dev, width, height, flags);
		} else {
			priv->bo = omap_bo_new_with_dim(pOMAP->dev, width, height, bitsPerPixel, flags);
		}
	}

	if (priv->bo) {
		pPixmap->devKind = omap_bo_pitch(priv->bo);
	}else{
		DEBUG_MSG("failed to allocate %dx%d bo, flags=%08x",
				width, height, flags);
	}

	return priv->bo != NULL;
}

/**
 * WaitMarker is a required EXA callback but synchronization is
 * performed during OMAPPrepareAccess so this function does not
 * have anything to do at present
 */
_X_EXPORT void
OMAPWaitMarker(ScreenPtr pScreen, int marker)
{
	/* no-op */
}

static inline enum omap_gem_op idx2op(int index)
{
	switch (index) {
	case EXA_PREPARE_SRC:
	case EXA_PREPARE_MASK:
	case EXA_PREPARE_AUX_SRC:
	case EXA_PREPARE_AUX_MASK:
		return OMAP_GEM_READ;
	case EXA_PREPARE_AUX_DEST:
	case EXA_PREPARE_DEST:
	default:
		return OMAP_GEM_READ | OMAP_GEM_WRITE;
	}
}

/**
 * PrepareAccess() is called before CPU access to an offscreen pixmap.
 *
 * @param pPix the pixmap being accessed
 * @param index the index of the pixmap being accessed.
 *
 * PrepareAccess() will be called before CPU access to an offscreen pixmap.
 * This can be used to set up hardware surfaces for byteswapping or
 * untiling, or to adjust the pixmap's devPrivate.ptr for the purpose of
 * making CPU access use a different aperture.
 *
 * The index is one of #EXA_PREPARE_DEST, #EXA_PREPARE_SRC,
 * #EXA_PREPARE_MASK, #EXA_PREPARE_AUX_DEST, #EXA_PREPARE_AUX_SRC, or
 * #EXA_PREPARE_AUX_MASK. Since only up to #EXA_NUM_PREPARE_INDICES pixmaps
 * will have PrepareAccess() called on them per operation, drivers can have
 * a small, statically-allocated space to maintain state for PrepareAccess()
 * and FinishAccess() in.  Note that PrepareAccess() is only called once per
 * pixmap and operation, regardless of whether the pixmap is used as a
 * destination and/or source, and the index may not reflect the usage.
 *
 * PrepareAccess() may fail.  An example might be the case of hardware that
 * can set up 1 or 2 surfaces for CPU access, but not 3.  If PrepareAccess()
 * fails, EXA will migrate the pixmap to system memory.
 * DownloadFromScreen() must be implemented and must not fail if a driver
 * wishes to fail in PrepareAccess().  PrepareAccess() must not fail when
 * pPix is the visible screen, because the visible screen can not be
 * migrated.
 *
 * @return TRUE if PrepareAccess() successfully prepared the pixmap for CPU
 * drawing.
 * @return FALSE if PrepareAccess() is unsuccessful and EXA should use
 * DownloadFromScreen() to migate the pixmap out.
 */
_X_EXPORT Bool
OMAPPrepareAccess(PixmapPtr pPixmap, int index)
{
	OMAPPixmapPrivPtr priv = exaGetPixmapDriverPrivate(pPixmap);

	pPixmap->devPrivate.ptr = omap_bo_map(priv->bo);
	if (!pPixmap->devPrivate.ptr) {
		return FALSE;
	}

	/* wait for blits complete.. note we could be a bit more clever here
	 * for non-DRI2 buffers and use separate OMAP{Prepare,Finish}GPUAccess()
	 * fxns wrapping accelerated GPU operations.. this way we don't have
	 * to prep/fini around each CPU operation, but only when there is an
	 * intervening GPU operation (or if we go to a stronger op mask, ie.
	 * first CPU access is READ and second is WRITE).
	 */

	if (omap_bo_cpu_prep(priv->bo, idx2op(index))) {
		return FALSE;
	}

	return TRUE;
}

/**
 * FinishAccess() is called after CPU access to an offscreen pixmap.
 *
 * @param pPix the pixmap being accessed
 * @param index the index of the pixmap being accessed.
 *
 * FinishAccess() will be called after finishing CPU access of an offscreen
 * pixmap set up by PrepareAccess().  Note that the FinishAccess() will not be
 * called if PrepareAccess() failed and the pixmap was migrated out.
 */
_X_EXPORT void
OMAPFinishAccess(PixmapPtr pPixmap, int index)
{
	OMAPPixmapPrivPtr priv = exaGetPixmapDriverPrivate(pPixmap);

	pPixmap->devPrivate.ptr = NULL;

	/* NOTE: can we use EXA migration module to track which parts of the
	 * buffer was accessed by sw, and pass that info down to kernel to
	 * do a more precise cache flush..
	 */
	omap_bo_cpu_fini(priv->bo, idx2op(index));
}

/**
 * PixmapIsOffscreen() is an optional driver replacement to
 * exaPixmapHasGpuCopy(). Set to NULL if you want the standard behaviour
 * of exaPixmapHasGpuCopy().
 *
 * @param pPix the pixmap
 * @return TRUE if the given drawable is in framebuffer memory.
 *
 * exaPixmapHasGpuCopy() is used to determine if a pixmap is in offscreen
 * memory, meaning that acceleration could probably be done to it, and that it
 * will need to be wrapped by PrepareAccess()/FinishAccess() when accessing it
 * with the CPU.
 */
_X_EXPORT Bool
OMAPPixmapIsOffscreen(PixmapPtr pPixmap)
{
	/* offscreen means in 'gpu accessible memory', not that it's off the
	 * visible screen.  We currently have no special constraints, since
	 * OMAP has a flat memory model (no separate GPU memory).  If
	 * individual EXA implementation has additional constraints, like
	 * buffer size or mapping in GPU MMU, it should wrap this function.
	 */
	OMAPPixmapPrivPtr priv = exaGetPixmapDriverPrivate(pPixmap);
	return priv && priv->bo;
}
