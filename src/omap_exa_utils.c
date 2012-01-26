/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright © 2012 Texas Instruments, Inc
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

#include "omap_driver.h"
#include "omap_exa.h"


/***** move to common helper code??  in EXA or DRI2? *****/
/* if this is in DRI2, then the driver can't rely on pixmap migration, etc
 * before PutTextureImage cb is called.. I guess it should be in EXA..
 */
#include <pixman.h>

/**
 * Helper function to implement video blit, handling clipping, damage, etc..
 *
 * TODO: move to EXA?
 */
int
OMAPVidCopyArea(DrawablePtr pSrcDraw, BoxPtr pSrcBox,
		DrawablePtr pOsdDraw, BoxPtr pOsdBox,
		DrawablePtr pDstDraw, BoxPtr pDstBox,
		OMAPPutTextureImageProc PutTextureImage, void *closure,
		RegionPtr clipBoxes)
{
	ScreenPtr pScreen = pDstDraw->pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	PixmapPtr pSrcPix = draw2pix(pSrcDraw);
	PixmapPtr pOsdPix = draw2pix(pOsdDraw);
	PixmapPtr pDstPix = draw2pix(pDstDraw);
	pixman_fixed_t sx, sy, tx, ty;
	pixman_transform_t srcxfrm;
	BoxPtr pbox;
	int nbox, dx, dy, ret = Success;

#ifdef COMPOSITE
	DEBUG_MSG("--> %dx%d, %dx%d", pDstPix->screen_x, pDstPix->screen_y,
			pDstDraw->x, pDstDraw->y);
	/* Convert screen coords to pixmap coords */
	if (pDstPix->screen_x || pDstPix->screen_y) {
		RegionTranslate(clipBoxes, -pDstPix->screen_x, -pDstPix->screen_y);
	}
	dx = pDstPix->screen_x;
	dy = pDstPix->screen_y;
#else
	dx = 0;
	dy = 0;
#endif

	/* the clip-region gives coordinates in dst's coord space..  generate
	 * a transform that can be used to work backwards from dst->src coord
	 * space:
	 */
	sx = ((pixman_fixed_48_16_t) (pSrcBox->x2 - pSrcBox->x1) << 16) /
			(pDstBox->x2 - pDstBox->x1);
	sy = ((pixman_fixed_48_16_t) (pSrcBox->y2 - pSrcBox->y1) << 16) /
			(pDstBox->y2 - pDstBox->y1);
	tx = ((pixman_fixed_48_16_t)(pDstBox->x1 - dx) << 16);
	ty = ((pixman_fixed_48_16_t)(pDstBox->y1 - dy) << 16);

	pixman_transform_init_scale(&srcxfrm, sx, sy);
	pixman_transform_translate(NULL, &srcxfrm, tx, ty);

	// TODO generate transform for osd as well

	pbox = RegionRects(clipBoxes);
	nbox = RegionNumRects(clipBoxes);

	while (nbox--) {
		RegionRec damage;
		BoxRec dstb = *pbox;
		BoxRec srcb = *pbox;
		BoxRec osdb = *pbox;

		pixman_transform_bounds(&srcxfrm, &srcb);
		//pixman_transform_bounds(&osdxfrm, &osdb);

		/* cropping is done in src coord space, post transform: */
		srcb.x1 += pSrcBox->x1;
		srcb.y1 += pSrcBox->y1;
		srcb.x2 += pSrcBox->x1;
		srcb.y2 += pSrcBox->y1;

		DEBUG_MSG("%d,%d %d,%d -> %d,%d %d,%d",
				srcb.x1, srcb.y1, srcb.x2, srcb.y2,
				dstb.x1, dstb.y1, dstb.x2, dstb.y2);

		ret = PutTextureImage(pSrcPix, &srcb, pOsdPix, &osdb,
				pDstPix, &dstb, closure);
		if (ret != Success) {
			break;
		}

		RegionInit(&damage, &dstb, 1);

#ifdef COMPOSITE
		/* Convert screen coords to pixmap coords */
		if (pDstPix->screen_x || pDstPix->screen_y) {
			RegionTranslate(&damage, pDstPix->screen_x, pDstPix->screen_y);
		}
#endif

		DamageRegionAppend(pDstDraw, &damage);
		RegionUninit(&damage);

		pbox++;
	}

	DamageRegionProcessPending(pDstDraw);

	return ret;
}
