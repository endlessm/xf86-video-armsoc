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
 *    Ian Elliott <ianelliottus@yahoo.com>
 *    Rob Clark <rob@ti.com>
 */

#ifndef __OMAP_DRV_H__
#define __OMAP_DRV_H__

/* All drivers need the following headers: */
#include "xf86.h"
#include "xf86_OSproc.h"

/* XXX - Perhaps, the following header files will only be used temporarily
 * (i.e. so we can use fbdevHW, SW cursor, etc):
 * XXX - figure out what can be removed..
 */
#include "mipointer.h"
#include "mibstore.h"
#include "micmap.h"
#include "colormapst.h"
#include "xf86cmap.h"
#include "shadow.h"
/* for visuals */
#include "fb.h"
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#include "xf86RAC.h"
#endif
#include "xf86xv.h"
#include "xf86Crtc.h"
#include "xf86RandR12.h"
#include "xf86drm.h"
#include "drm_fourcc.h"
#include "dri2.h"

#include <omap_drm.h>
#include <omap_drmif.h>

#include <errno.h>

#include "omap_exa.h"


#define OMAP_VERSION		1000	/* Apparently not used by X server */
#define OMAP_NAME			"OMAP"	/* Name used to prefix messages */
#define OMAP_DRIVER_NAME	"omap"	/* Driver name as used in config file */
#define OMAP_MAJOR_VERSION	0
#define OMAP_MINOR_VERSION	83
#define OMAP_PATCHLEVEL		0

/**
 * This controls whether debug statements (and function "trace" enter/exit)
 * messages are sent to the log file (TRUE) or are ignored (FALSE).
 */
extern _X_EXPORT Bool omapDebug;


/* Various logging/debug macros for use in the X driver and the external
 * sub-modules:
 */
#define TRACE_ENTER() \
		do { if (omapDebug) xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s:%d: Entering\n",\
				__FUNCTION__, __LINE__); } while (0)
#define TRACE_EXIT() \
		do { if (omapDebug) xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s:%d: Exiting\n",\
				__FUNCTION__, __LINE__); } while (0)
#define DEBUG_MSG(fmt, ...) \
		do { if (omapDebug) xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s:%d " fmt "\n",\
				__FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)
#define INFO_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, X_INFO, fmt "\n",\
				##__VA_ARGS__); } while (0)
#define CONFIG_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, fmt "\n",\
				##__VA_ARGS__); } while (0)
#define WARNING_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "WARNING: " fmt "\n",\
				##__VA_ARGS__); } while (0)
#define ERROR_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "ERROR: " fmt "\n",\
				##__VA_ARGS__); } while (0)
#define EARLY_ERROR_MSG(fmt, ...) \
		do { xf86Msg(X_ERROR, "ERROR: " fmt "\n",\
				##__VA_ARGS__); } while (0)


/* Forward declarations: */
struct _OMAPRec;

extern unsigned int
OMAPCalculateStride(unsigned int fbWidth, unsigned int bitsPerPixel);
extern unsigned int
OMAPCalculateTiledStride(unsigned int width, unsigned int bitsPerPixel);



/** The driver's Screen-specific, "private" data structure. */
typedef struct _OMAPRec
{
	/** Chipset id */
	int					chipset;

	/**
	 * Pointer to a structure used to communicate and coordinate with an
	 * external EXA library (if loaded).
	 */
	OMAPEXAPtr			pOMAPEXA;

	/** various user-configurable options: */
	Bool				dri;
	Bool				HWCursor;
	Bool				NoAccel;

	/** File descriptor of the connection with the DRM. */
	int					drmFD;

	char 				*deviceName;

	/** DRM device instance */
	struct omap_device	*dev;

	/** Scan-out buffer. */
	struct omap_bo		*scanout;

	/** Pointer to the options for this screen. */
	OptionInfoPtr		pOptionInfo;

	/** Save (wrap) the original pScreen functions. */
	CloseScreenProcPtr				SavedCloseScreen;
	CreateScreenResourcesProcPtr	SavedCreateScreenResources;
	ScreenBlockHandlerProcPtr		SavedBlockHandler;

	/** Pointer to the entity structure for this screen. */
	EntityInfoPtr		pEntityInfo;

	XF86VideoAdaptorPtr textureAdaptor;

	/** Flips we are waiting for: */
	int					pending_flips;

} OMAPRec, *OMAPPtr;

/*
 * Misc utility macros:
 */

/* do we support video? */
static inline Bool has_video(OMAPPtr pOMAP)
{
	return pOMAP->pOMAPEXA &&
			pOMAP->pOMAPEXA->GetFormats &&
			pOMAP->pOMAPEXA->PutTextureImage;
}

static inline Bool has_dmm(OMAPPtr pOMAP)
{
	return pOMAP->chipset >= 0x4430;
}

/** Return a pointer to the driver's private structure. */
#define OMAPPTR(p) ((OMAPPtr)((p)->driverPrivate))
#define OMAPPTR_FROM_SCREEN(pScreen) \
	((OMAPPtr)(xf86Screens[(pScreen)->myNum])->driverPrivate);

#define wrap(priv, real, mem, func) {\
    priv->Saved##mem = real->mem; \
    real->mem = func; \
}

#define unwrap(priv, real, mem) {\
    real->mem = priv->Saved##mem; \
}

#define swap(priv, real, mem) {\
    void *tmp = priv->Saved##mem; \
    priv->Saved##mem = real->mem; \
    real->mem = tmp; \
}

#define exchange(a, b) {\
	typeof(a) tmp = a; \
	a = b; \
	b = tmp; \
}

#ifndef ARRAY_SIZE
#  define ARRAY_SIZE(a)  (sizeof(a) / sizeof(a[0]))
#endif
#define ALIGN(val, align)	(((val) + (align) - 1) & ~((align) - 1))


/**
 * drmmode functions..
 */
Bool drmmode_pre_init(ScrnInfoPtr pScrn, int fd, int cpp);
void drmmode_screen_init(ScrnInfoPtr pScrn);
void drmmode_screen_fini(ScrnInfoPtr pScrn);
void drmmode_adjust_frame(ScrnInfoPtr pScrn, int x, int y, int flags);
void drmmode_remove_fb(ScrnInfoPtr pScrn);
Bool drmmode_page_flip(DrawablePtr draw, uint32_t fb_id, void *priv);
void drmmode_wait_for_event(ScrnInfoPtr pScrn);
Bool drmmode_cursor_init(ScreenPtr pScreen);


/**
 * DRI2 functions..
 */
typedef struct _OMAPDRISwapCmd OMAPDRISwapCmd;
Bool OMAPDRI2ScreenInit(ScreenPtr pScreen);
void OMAPDRI2CloseScreen(ScreenPtr pScreen);
void OMAPDRI2SwapComplete(OMAPDRISwapCmd *cmd);

/**
 * XV functions..
 */
Bool OMAPVideoScreenInit(ScreenPtr pScreen);
void OMAPVideoCloseScreen(ScreenPtr pScreen);

/**
 * EXA util functions.. move to EXA core?
 */

typedef int (*OMAPPutTextureImageProc)(
		PixmapPtr pSrcPix, BoxPtr pSrcBox,
		PixmapPtr pOsdPix, BoxPtr pOsdBox,
		PixmapPtr pDstPix, BoxPtr pDstBox,
		void *closure);

Bool OMAPVidCopyArea(DrawablePtr pSrcDraw, BoxPtr pSrcBox,
		DrawablePtr pOsdDraw, BoxPtr pOsdBox,
		DrawablePtr pDstDraw, BoxPtr pDstBox,
		OMAPPutTextureImageProc PutTextureImage, void *closure,
		RegionPtr clipBoxes);

#endif /* __OMAP_DRV_H__ */
