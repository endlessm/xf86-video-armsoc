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

#ifndef OMAP_EXA_COMMON_H_
#define OMAP_EXA_COMMON_H_

#include "omap_driver.h"
#include "exa.h"

/* Common OMAP EXA functions, mostly related to pixmap/buffer allocation.
 * Individual driver submodules can use these directly, or wrap them with
 * there own functions if anything additional is required.  Submodules
 * can use OMAPPrixmapPrivPtr#priv for their own private data.
 */

typedef struct {
	void *priv;			/* EXA submodule private data */
	struct omap_bo *bo;
} OMAPPixmapPrivRec, *OMAPPixmapPrivPtr;

#define OMAP_CREATE_PIXMAP_SCANOUT 0x80000000
#define OMAP_CREATE_PIXMAP_TILED   0x40000000


void * OMAPCreatePixmap (ScreenPtr pScreen, int width, int height,
		int depth, int usage_hint, int bitsPerPixel,
		int *new_fb_pitch);
void OMAPDestroyPixmap(ScreenPtr pScreen, void *driverPriv);
Bool OMAPModifyPixmapHeader(PixmapPtr pPixmap, int width, int height,
		int depth, int bitsPerPixel, int devKind,
		pointer pPixData);
void OMAPWaitMarker(ScreenPtr pScreen, int marker);
Bool OMAPPrepareAccess(PixmapPtr pPixmap, int index);
void OMAPFinishAccess(PixmapPtr pPixmap, int index);
Bool OMAPPixmapIsOffscreen(PixmapPtr pPixmap);

static inline struct omap_bo *
OMAPPixmapBo(PixmapPtr pPixmap)
{
	OMAPPixmapPrivPtr priv = exaGetPixmapDriverPrivate(pPixmap);
	return priv->bo;
}

/* used by DRI2 code to play buffer switcharoo */
static inline void
OMAPPixmapExchange(PixmapPtr a, PixmapPtr b)
{
	OMAPPixmapPrivPtr apriv = exaGetPixmapDriverPrivate(a);
	OMAPPixmapPrivPtr bpriv = exaGetPixmapDriverPrivate(b);
	exchange(apriv->priv, bpriv->priv);
	exchange(apriv->bo, bpriv->bo);
}

#endif /* OMAP_EXA_COMMON_H_ */
