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

#ifndef ARMSOC_EXA_COMMON_H_
#define ARMSOC_EXA_COMMON_H_

/* note: don't include "armsoc_driver.h" here.. we want to keep some
 * isolation between structs shared with submodules and stuff internal
 * to core driver..
 */
#include "armsoc_dumb.h"
#include "xf86.h"
#include "xf86_OSproc.h"
#include "exa.h"
#include "compat-api.h"

/**
 * A per-Screen structure used to communicate and coordinate between the
 * ARMSOC X driver and an external EXA sub-module (if loaded).
 */
struct ARMSOCEXARec {
	/**
	 * Called by X driver's CloseScreen() function at the end of each server
	 * generation to free per-Screen data structures (except those held by
	 * pScrn).
	 */
	Bool (*CloseScreen)(CLOSE_SCREEN_ARGS_DECL);

	/**
	 * Called by X driver's FreeScreen() function at the end of each
	 * server lifetime to free per-ScrnInfoRec data structures, to close
	 * any external connections (e.g. with PVR2D, DRM), etc.
	 */
	void (*FreeScreen)(FREE_SCREEN_ARGS_DECL);

	/* add new fields here at end, to preserve ABI */

};

/**
 * Fallback EXA implementation
 */
struct ARMSOCEXARec *InitNullEXA(ScreenPtr pScreen, ScrnInfoPtr pScrn, int fd);


struct ARMSOCEXARec *ARMSOCEXAPTR(ScrnInfoPtr pScrn);

static inline ScrnInfoPtr
pix2scrn(PixmapPtr pPixmap)
{
	ScreenPtr pScreen = (pPixmap)->drawable.pScreen;
	return xf86ScreenToScrn(pScreen);
}

static inline PixmapPtr
draw2pix(DrawablePtr pDraw)
{
	if (!pDraw)
		return NULL;
	else if (pDraw->type == DRAWABLE_WINDOW)
		return pDraw->pScreen->GetWindowPixmap((WindowPtr)pDraw);
	else
		return (PixmapPtr)pDraw;
}

/* Common ARMSOC EXA functions, mostly related to pixmap/buffer allocation.
 * Individual driver submodules can use these directly, or wrap them with
 * there own functions if anything additional is required.  Submodules
 * can use ARMSOCPrixmapPrivPtr#priv for their own private data.
 */

struct ARMSOCPixmapPrivRec {
	/* EXA submodule private data */
	void *priv;
	/* Ref-count of DRI2Buffers that wrap the Pixmap,
	 * that allow external access to the underlying
	 * buffer. When >0 CPU access must be synchronised.
	 */
	int ext_access_cnt;
	struct armsoc_bo *bo;
	int usage_hint;
};


#define ARMSOC_CREATE_PIXMAP_SCANOUT 0x80000000


void *ARMSOCCreatePixmap2(ScreenPtr pScreen, int width, int height,
		int depth, int usage_hint, int bitsPerPixel,
		int *new_fb_pitch);
void ARMSOCDestroyPixmap(ScreenPtr pScreen, void *driverPriv);
Bool ARMSOCModifyPixmapHeader(PixmapPtr pPixmap, int width, int height,
		int depth, int bitsPerPixel, int devKind,
		pointer pPixData);
void ARMSOCWaitMarker(ScreenPtr pScreen, int marker);
Bool ARMSOCPrepareAccess(PixmapPtr pPixmap, int index);
void ARMSOCFinishAccess(PixmapPtr pPixmap, int index);
Bool ARMSOCPixmapIsOffscreen(PixmapPtr pPixmap);

static inline struct armsoc_bo *
ARMSOCPixmapBo(PixmapPtr pPixmap)
{
	struct ARMSOCPixmapPrivRec *priv = exaGetPixmapDriverPrivate(pPixmap);
	return priv->bo;
}

void ARMSOCPixmapExchange(PixmapPtr a, PixmapPtr b);

/* Register that the pixmap can be accessed externally, so
 * CPU access must be synchronised. */
void ARMSOCRegisterExternalAccess(PixmapPtr pPixmap);
void ARMSOCDeregisterExternalAccess(PixmapPtr pPixmap);


#endif /* ARMSOC_EXA_COMMON_H_ */
