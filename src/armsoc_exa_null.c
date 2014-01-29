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

#include "exa.h"

/* This file has a trivial EXA implementation which accelerates nothing.  It
 * is used as the fall-back in case the EXA implementation for the current
 * chipset is not available.  (For example, on chipsets which used the closed
 * source IMG PowerVR EXA implementation, if the closed-source submodule is
 * not installed.
 */

struct ARMSOCNullEXARec {
	struct ARMSOCEXARec base;
	ExaDriverPtr exa;
	/* add any other driver private data here.. */
};

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
		PicturePtr pDstPicture, PixmapPtr pSrc,
		PixmapPtr pMask, PixmapPtr pDst)
{
	return FALSE;
}

/**
 * CloseScreen() is called at the end of each server generation and
 * cleans up everything initialised in InitNullEXA()
 */
static Bool
CloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	exaDriverFini(pScreen);
	free(((struct ARMSOCNullEXARec *)pARMSOC->pARMSOCEXA)->exa);
	free(pARMSOC->pARMSOCEXA);
	pARMSOC->pARMSOCEXA = NULL;

	return TRUE;
}

/* FreeScreen() is called on an error during PreInit and
 * should clean up anything initialised before InitNullEXA()
 * (which currently is nothing)
 *
 */
static void
FreeScreen(FREE_SCREEN_ARGS_DECL)
{
}

struct ARMSOCEXARec *
InitNullEXA(ScreenPtr pScreen, ScrnInfoPtr pScrn, int fd)
{
	struct ARMSOCNullEXARec *null_exa;
	struct ARMSOCEXARec *armsoc_exa;
	ExaDriverPtr exa;

	INFO_MSG("Soft EXA mode");

	null_exa = calloc(1, sizeof(*null_exa));
	if (!null_exa)
		goto out;

	armsoc_exa = (struct ARMSOCEXARec *)null_exa;

	exa = exaDriverAlloc();
	if (!exa)
		goto free_null_exa;

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
	exa->WaitMarker = ARMSOCWaitMarker;
	exa->CreatePixmap2 = ARMSOCCreatePixmap2;
	exa->DestroyPixmap = ARMSOCDestroyPixmap;
	exa->ModifyPixmapHeader = ARMSOCModifyPixmapHeader;

	exa->PrepareAccess = ARMSOCPrepareAccess;
	exa->FinishAccess = ARMSOCFinishAccess;
	exa->PixmapIsOffscreen = ARMSOCPixmapIsOffscreen;

	/* Always fallback for software operations */
	exa->PrepareCopy = PrepareCopyFail;
	exa->PrepareSolid = PrepareSolidFail;
	exa->CheckComposite = CheckCompositeFail;
	exa->PrepareComposite = PrepareCompositeFail;

	if (!exaDriverInit(pScreen, exa)) {
		ERROR_MSG("exaDriverInit failed");
		goto free_exa;
	}

	armsoc_exa->CloseScreen = CloseScreen;
	armsoc_exa->FreeScreen = FreeScreen;

	return armsoc_exa;

free_exa:
	free(exa);
free_null_exa:
	free(null_exa);
out:
	return NULL;
}

