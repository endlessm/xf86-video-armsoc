/*
 * Copyright (C) 2015 Endless Mobile
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "armsoc_driver.h"
#include "armsoc_exa.h"
#include "fb.h"

DevPrivateKeyRec alphaHackGCPrivateKeyRec;
#define alphaHackGCPrivateKey (&alphaHackGCPrivateKeyRec)

DevPrivateKeyRec alphaHackScreenPrivateKeyRec;
#define alphaHackScreenPrivateKey (&alphaHackScreenPrivateKeyRec)

// AlphaHackGCRec: private per-gc data
typedef struct {
    GCFuncs funcs;
    const GCFuncs *origFuncs;

    GCOps ops;
    const GCOps *origOps;
} AlphaHackGCRec;

// AlphaHackScreenRec: per-screen private data
typedef struct _AlphaHackScreenRec {
    CreateGCProcPtr CreateGC;
} AlphaHackScreenRec, *AlphaHackScreenPtr;

static inline PixmapPtr
GetDrawablePixmap(DrawablePtr pDrawable)
{
    if (pDrawable->type == DRAWABLE_PIXMAP)
        return (PixmapPtr) pDrawable;
    else
        return pDrawable->pScreen->GetWindowPixmap((WindowPtr) pDrawable);
}

static inline Bool
IsPixmapScanout(PixmapPtr pPixmap)
{
    struct ARMSOCPixmapPrivRec *priv = exaGetPixmapDriverPrivate(pPixmap);
    return !!(priv->usage_hint & ARMSOC_CREATE_PIXMAP_SCANOUT);
}

static inline Bool
IsDrawableScanout(DrawablePtr pDrawable)
{
    return IsPixmapScanout(GetDrawablePixmap(pDrawable));
}

static inline Bool
ShouldApplyAlphaHack(DrawablePtr pDrawable)
{
    /* Do some quick early checks before we dig into the drawable
     * more deeply. */
    if (pDrawable->depth != 24 || pDrawable->bitsPerPixel != 32 ||
        pDrawable->type != DRAWABLE_WINDOW)
        return FALSE;

    return IsDrawableScanout(pDrawable);
}

#define UNWRAP_FUNCS() pGC->funcs = gcrec->origFuncs;
#define WRAP_FUNCS() pGC->funcs = &gcrec->funcs;

#define UNWRAP_OPS() pGC->ops = gcrec->origOps;
#define WRAP_OPS() pGC->ops = &gcrec->ops;

static void
AlphaHackValidateGC(GCPtr pGC, unsigned long changes, DrawablePtr pDrawable)
{
    AlphaHackGCRec *gcrec = dixLookupPrivate(&pGC->devPrivates, alphaHackGCPrivateKey);
    unsigned int depth = pDrawable->depth;

    UNWRAP_FUNCS();

    /* If we're drawing to a scanout bo, make sure that
     * we don't overwrite the alpha mask. */
    if (ShouldApplyAlphaHack(pDrawable)) {
        long unsigned int pm = pGC->planemask;
        pGC->planemask &= 0x00FFFFFF;
        if (pm != pGC->planemask) {
            changes |= GCPlaneMask;
            pDrawable->depth = pDrawable->bitsPerPixel;
        }
    }
    pGC->funcs->ValidateGC(pGC, changes, pDrawable);
    pDrawable->depth = depth;

    WRAP_FUNCS();
}

static void
AlphaHackCopyNToN(DrawablePtr pSrcDrawable,
                  DrawablePtr pDstDrawable,
                  GCPtr pGC, BoxPtr pbox, int nbox,
                  int dx, int dy,
                  Bool reverse, Bool upsidedown, Pixel bitplane, void *closure)
{
    FbBits *src;
    FbStride srcStride;
    int srcBpp;
    int srcXoff, srcYoff;
    FbBits *dst;
    FbStride dstStride;
    int dstBpp;
    int dstXoff, dstYoff;
    int n;
    BoxPtr b;
    pixman_image_t *si, *di;

    fbGetDrawable(pSrcDrawable, src, srcStride, srcBpp, srcXoff, srcYoff);
    fbGetDrawable(pDstDrawable, dst, dstStride, dstBpp, dstXoff, dstYoff);

    si = pixman_image_create_bits(PIXMAN_x8r8g8b8,
                                  pSrcDrawable->width, pSrcDrawable->height,
                                  (uint32_t *) src, srcStride * sizeof(FbStride));
    di = pixman_image_create_bits(PIXMAN_a8r8g8b8,
                                  pDstDrawable->width, pDstDrawable->height,
                                  (uint32_t *) dst, dstStride * sizeof(FbStride));

    n = nbox;
    b = pbox;
    while (n--) {
        pixman_image_composite32(PIXMAN_OP_SRC, si, NULL, di,
                                 (b->x1 + dx + srcXoff), (b->y1 + dy + srcYoff),
                                 0, 0,
                                 (b->x1 + dstXoff), (b->y1 + dstYoff),
                                 (b->x2 - b->x1), (b->y2 - b->y1));

        b++;
    }

    pixman_image_unref(si);
    pixman_image_unref(di);
}

static RegionPtr
AlphaHackCopyArea(DrawablePtr pSrcDrawable,
                  DrawablePtr pDstDrawable,
                  GCPtr pGC,
                  int xIn, int yIn, int widthSrc, int heightSrc,
                  int xOut, int yOut)
{
    AlphaHackGCRec *gcrec = dixLookupPrivate(&pGC->devPrivates, alphaHackGCPrivateKey);
    FbBits pm = fbGetGCPrivate(pGC)->pm;
    if (pGC->alu == GXcopy && pm == 0x00FFFFFF && ShouldApplyAlphaHack(pDstDrawable))
        return miDoCopy(pSrcDrawable, pDstDrawable, pGC, xIn, yIn,
                        widthSrc, heightSrc, xOut, yOut, AlphaHackCopyNToN, 0, 0);
    else
        return gcrec->origOps->CopyArea(pSrcDrawable, pDstDrawable, pGC, xIn, yIn,
                                        widthSrc, heightSrc, xOut, yOut);
}

static Bool
AlphaHackDoPutImage(DrawablePtr pDrawable, GCPtr pGC, int depth,
                    int x, int y, int w, int h, int format, char *bits)
{
    pixman_image_t *si, *di;
    FbBits *dst;
    FbStride dstStride;
    int dstBpp;
    int dstXoff, dstYoff;
    RegionPtr pClip;
    BoxPtr pbox;
    int nbox;
    int bpp = pDrawable->bitsPerPixel;

    if (format != ZPixmap || bpp != 32 || pGC->alu != GXcopy)
        return FALSE;
    ErrorF("PI2\n");
    if (!ShouldApplyAlphaHack(pDrawable))
        return FALSE;
    ErrorF("PI3\n");

    fbGetDrawable(pDrawable, dst, dstStride, dstBpp, dstXoff, dstYoff);

    si = pixman_image_create_bits(PIXMAN_x8r8g8b8, w, h, (uint32_t *) bits, w * sizeof(FbStride));
    di = pixman_image_create_bits(PIXMAN_a8r8g8b8,
                                  pDrawable->width, pDrawable->height,
                                  (uint32_t *) dst, dstStride * sizeof(FbStride));

    pClip = fbGetCompositeClip(pGC);

    for (nbox = RegionNumRects(pClip), pbox = RegionRects(pClip); nbox--; pbox++) {
        int x1 = x;
        int y1 = y;
        int x2 = x + w;
        int y2 = y + h;

        if (x1 < pbox->x1)
            x1 = pbox->x1;
        if (y1 < pbox->y1)
            y1 = pbox->y1;
        if (x2 > pbox->x2)
            x2 = pbox->x2;
        if (y2 > pbox->y2)
            y2 = pbox->y2;
        if (x1 >= x2 || y1 >= y2)
            continue;

        pixman_image_composite32(PIXMAN_OP_SRC, si, NULL, di,
                                 (x1 - x), (y1 - y),
                                 0, 0,
                                 (x1 + dstXoff), (y1 + dstYoff),
                                 (x2 - x1), (y2 - y1));
    }

    pixman_image_unref(si);
    pixman_image_unref(di);
    return TRUE;
}

static void
AlphaHackPutImage(DrawablePtr pDrawable,
                  GCPtr pGC,
                  int depth, int x, int y, int w, int h,
                  int leftPad, int format, char *bits)
{
    AlphaHackGCRec *gcrec = dixLookupPrivate(&pGC->devPrivates, alphaHackGCPrivateKey);

    if (!AlphaHackDoPutImage(pDrawable, pGC, depth, x, y, w, h, format, bits))
        gcrec->origOps->PutImage(pDrawable, pGC, depth, x, y, w, h, leftPad, format, bits);
}

static Bool
AlphaHackCreateGC(GCPtr pGC)
{
    ScreenPtr pScreen = pGC->pScreen;
    AlphaHackGCRec *gcrec;
    AlphaHackScreenRec *s;
    Bool result;

    s = dixLookupPrivate(&pScreen->devPrivates, alphaHackScreenPrivateKey);
    pScreen->CreateGC = s->CreateGC;
    result = pScreen->CreateGC(pGC);
    gcrec = dixLookupPrivate(&pGC->devPrivates, alphaHackGCPrivateKey);

    gcrec->origFuncs = pGC->funcs;
    gcrec->funcs = *pGC->funcs;
    gcrec->funcs.ValidateGC = AlphaHackValidateGC;

    gcrec->origOps = pGC->ops;
    gcrec->ops = *pGC->ops;
    gcrec->ops.CopyArea = AlphaHackCopyArea;
    gcrec->ops.PutImage = AlphaHackPutImage;

    WRAP_FUNCS();
    WRAP_OPS();

    pScreen->CreateGC = AlphaHackCreateGC;

    return result;
}

Bool InstallAlphaHack(ScreenPtr pScreen);

Bool
InstallAlphaHack(ScreenPtr pScreen)
{
    AlphaHackScreenRec *s;

    if (!dixRegisterPrivateKey(&alphaHackGCPrivateKeyRec, PRIVATE_GC, sizeof(AlphaHackGCRec)))
        return FALSE;
    if (!dixRegisterPrivateKey(&alphaHackScreenPrivateKeyRec, PRIVATE_SCREEN, 0))
        return FALSE;

    s = malloc(sizeof(AlphaHackScreenRec));
    if (!s)
        return FALSE;
    dixSetPrivate(&pScreen->devPrivates, alphaHackScreenPrivateKey, s);

    s->CreateGC = pScreen->CreateGC;
    pScreen->CreateGC = AlphaHackCreateGC;

    return TRUE;
}
