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

#include "omap_driver.h"
#include "omap_exa.h"

#include "exa.h"

/* This file has a trivial EXA implementation which accelerates nothing.  It
 * is used as the fall-back in case the EXA implementation for the current
 * chipset is not available.  (For example, on chipsets which used the closed
 * source IMG PowerVR EXA implementation, if the closed-source submodule is
 * not installed.
 */

typedef struct {
	OMAPEXARec base;
	ExaDriverPtr exa;
	/* add any other driver private data here.. */
} OMAPNullEXARec, *OMAPNullEXAPtr;


static Bool
PrepareSolidFail(PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fill_colour)
{
	return FALSE;
}

static Bool
PrepareCopyFail(PixmapPtr pSrc, PixmapPtr pDst, int xdir, int ydir,
		int alu, Pixel planemask)
{
	return FALSE;
}

static Bool
CheckCompositeFail(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
		PicturePtr pDstPicture)
{
	return FALSE;
}

static Bool
PrepareCompositeFail(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
		PicturePtr pDstPicture, PixmapPtr pSrc, PixmapPtr pMask, PixmapPtr pDst)
{
	return FALSE;
}

static Bool
CloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
#if 0 // TODO need to change CloseScreen/FreeScreen ..
	exaDriverFini(pScreen);
	free(pNv->EXADriverPtr);
#endif
	return TRUE;
}

static void
FreeScreen(FREE_SCREEN_ARGS_DECL)
{
}


OMAPEXAPtr
InitNullEXA(ScreenPtr pScreen, ScrnInfoPtr pScrn, int fd)
{
	OMAPNullEXAPtr null_exa = calloc(sizeof (*null_exa), 1);
	OMAPEXAPtr omap_exa = (OMAPEXAPtr)null_exa;
	ExaDriverPtr exa;

	INFO_MSG("Soft EXA mode");

	exa = exaDriverAlloc();
	if (!exa) {
		goto fail;
	}

	null_exa->exa = exa;

	exa->exa_major = EXA_VERSION_MAJOR;
	exa->exa_minor = EXA_VERSION_MINOR;

	exa->pixmapOffsetAlign = 0;
	exa->pixmapPitchAlign = 32;
	exa->flags = EXA_OFFSCREEN_PIXMAPS |
			EXA_HANDLES_PIXMAPS | EXA_SUPPORTS_PREPARE_AUX;
	exa->maxX = 4096;
	exa->maxY = 4096;

	/* Required EXA functions: */
	exa->WaitMarker = OMAPWaitMarker;
	exa->CreatePixmap2 = OMAPCreatePixmap;
	exa->DestroyPixmap = OMAPDestroyPixmap;
	exa->ModifyPixmapHeader = OMAPModifyPixmapHeader;

	exa->PrepareAccess = OMAPPrepareAccess;
	exa->FinishAccess = OMAPFinishAccess;
	exa->PixmapIsOffscreen = OMAPPixmapIsOffscreen;

	// Always fallback for software operations
	exa->PrepareCopy = PrepareCopyFail;
	exa->PrepareSolid = PrepareSolidFail;
	exa->CheckComposite = CheckCompositeFail;
	exa->PrepareComposite = PrepareCompositeFail;

	if (! exaDriverInit(pScreen, exa)) {
		ERROR_MSG("exaDriverInit failed");
		goto fail;
	}

	omap_exa->CloseScreen = CloseScreen;
	omap_exa->FreeScreen = FreeScreen;

	return omap_exa;

fail:
	if (null_exa) {
		free(null_exa);
	}
	return NULL;
}

