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

DevPrivateKeyRec alphaHackGCPrivateKeyRec;
#define alphaHackGCPrivateKey (&alphaHackGCPrivateKeyRec)

DevPrivateKeyRec alphaHackScreenPrivateKeyRec;
#define alphaHackScreenPrivateKey (&alphaHackScreenPrivateKeyRec)

// AlphaHackGCRec: private per-gc data
typedef struct {
    GCFuncs funcs;

    void *ValidateGC;
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

static void
AlphaHackValidateGC(GCPtr pGC, unsigned long changes, DrawablePtr pDrawable)
{
    AlphaHackGCRec *gcrec = dixLookupPrivate(&pGC->devPrivates, alphaHackGCPrivateKey);
    unsigned int depth = pDrawable->depth;

    gcrec->funcs.ValidateGC = gcrec->ValidateGC;

    /* If we're drawing to a scanout bo, make sure that
     * we don't overwrite the alpha mask. */
    if (pDrawable->depth == 24 && pDrawable->bitsPerPixel == 32 &&
        pDrawable->type == DRAWABLE_WINDOW && IsDrawableScanout(pDrawable)) {
        long unsigned int pm = pGC->planemask;
        pGC->planemask &= 0x00FFFFFF;
        if (pm != pGC->planemask) {
            changes |= GCPlaneMask;
            pDrawable->depth = pDrawable->bitsPerPixel;
        }
    }
    pGC->funcs->ValidateGC(pGC, changes, pDrawable);
    pDrawable->depth = depth;

    gcrec->funcs.ValidateGC = AlphaHackValidateGC;
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
    gcrec->ValidateGC = pGC->funcs->ValidateGC;
    gcrec->funcs = *pGC->funcs;
    gcrec->funcs.ValidateGC = AlphaHackValidateGC;
    pGC->funcs = &gcrec->funcs;
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
