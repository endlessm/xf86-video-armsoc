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

#include "omap_exa_common.h"

#include "exa.h"


#define pix2scrn(pPixmap) \
		xf86Screens[(pPixmap)->drawable.pScreen->myNum]


/* Common OMAP EXA functions, mostly related to pixmap/buffer allocation.
 * Individual driver submodules can use these directly, or wrap them with
 * there own functions if anything additional is required.  Submodules
 * can use OMAPPrixmapPrivPtr#priv for their own private data.
 */

void *
OMAPCreatePixmap (ScreenPtr pScreen, int width, int height,
		int depth, int usage_hint, int bitsPerPixel,
		int *new_fb_pitch)
{
	OMAPPixmapPrivPtr priv = calloc(sizeof(OMAPPixmapPrivRec), 1);

	/* actual allocation of buffer is in OMAPModifyPixmapHeader */

	return priv;
}

void
OMAPDestroyPixmap(ScreenPtr pScreen, void *driverPriv)
{
	OMAPPixmapPrivPtr priv = driverPriv;

	if (priv->bo) {
		omap_bo_del(priv->bo);
	}

	free(priv);
}

Bool
OMAPModifyPixmapHeader(PixmapPtr pPixmap, int width, int height,
		int depth, int bitsPerPixel, int devKind,
		pointer pPixData)
{
	OMAPPixmapPrivPtr priv = exaGetPixmapDriverPrivate(pPixmap);
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	OMAPPtr pOMAP = OMAPPTR(pScrn);
	uint32_t size;
	Bool ret;

	ret = miModifyPixmapHeader(pPixmap, width, height, depth,
			bitsPerPixel, devKind, pPixData);
	if (!ret) {
		return ret;
	}

	if (pPixData == omap_bo_map(pOMAP->scanout)) {
		DEBUG_MSG("wrapping scanout buffer");
		if (priv->bo != pOMAP->scanout) {
			omap_bo_del(priv->bo);
			priv->bo = pOMAP->scanout;
		}
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


	pPixmap->devKind = OMAPCalculateStride(width, bitsPerPixel);
	size = pPixmap->devKind * height;

	if ((!priv->bo) || (omap_bo_size(priv->bo) != size)) {
		/* re-allocate buffer! */
		omap_bo_del(priv->bo);
		priv->bo = omap_bo_new(pOMAP->dev, size, 0);
	}

	return TRUE;
}

/**
 * WaitMarker is a required EXA callback but synchronization is
 * performed during OMAPPrepareAccess so this function does not
 * have anything to do at present
 */
void
OMAPWaitMarker(ScreenPtr pScreen, int marker)
{
	/* no-op */
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
Bool
OMAPPrepareAccess(PixmapPtr pPixmap, int index)
{
	OMAPPixmapPrivPtr priv = exaGetPixmapDriverPrivate(pPixmap);

	/* TODO: wait for blits complete on priv->bo.. */

	pPixmap->devPrivate.ptr = omap_bo_map(priv->bo);

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
void
OMAPFinishAccess(PixmapPtr pPixmap, int index)
{
	pPixmap->devPrivate.ptr = NULL;
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
Bool
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
