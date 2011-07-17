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

#ifndef OMAP_DRV_H_
#define OMAP_DRV_H_

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
#ifdef XF86DRI
#define _XF86DRI_SERVER_
#include "dri2.h"
#endif

#include "omap_drm.h"

#include <errno.h>




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


/**
 * A per-Screen structure used to communicate and coordinate between the OMAP X
 * driver and an external EXA sub-module (if loaded).
 */
typedef struct _OMAPEXARec
{
	union { struct {

	/**
	 * Called by X driver's CloseScreen() function at the end of each server
	 * generation to free per-Screen data structures (except those held by
	 * pScrn).
	 */
	Bool (*CloseScreen)(int scrnIndex, ScreenPtr pScreen);

	/**
	 * Called by X driver's FreeScreen() function at the end of each server
	 * lifetime to free per-ScrnInfoRec data structures, to close any external
	 * connections (e.g. with PVR2D, DRM), etc.
	 */
	void (*FreeScreen)(int scrnIndex, int flags);

	/* add new fields here at end, to preserve ABI */

	};

	/* padding to keep ABI stable, so an existing EXA submodule
	 * doesn't need to be recompiled when new fields are added
	 */
	void *pad[64];
	};

} OMAPEXARec, *OMAPEXAPtr;



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

	Bool				NoAccel;

	/** File descriptor of the connection with the DRM. */
	int					drmFD;

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

} OMAPRec, *OMAPPtr;

/*
 * Misc utility macros:
 */

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


/**
 * Canonical name of an external sub-module providing support for EXA
 * acceleration, that utiltizes the OMAP's PowerVR accelerator and uses closed
 * source from Imaginations Technology Limited.
 */
#define SUB_MODULE_PVR	"omap_pvr"
OMAPEXAPtr InitPowerVREXA(ScreenPtr pScreen, ScrnInfoPtr pScrn);

/**
 * Fallback EXA implementation
 */
OMAPEXAPtr InitNullEXA(ScreenPtr pScreen, ScrnInfoPtr pScrn);

/**
 * drmmode functions..
 */
Bool drmmode_pre_init(ScrnInfoPtr pScrn, int fd, int cpp);
void drmmode_uevent_init(ScrnInfoPtr pScrn);
void drmmode_uevent_fini(ScrnInfoPtr pScrn);
void drmmode_adjust_frame(ScrnInfoPtr pScrn, int x, int y, int flags);
void drmmode_remove_fb(ScrnInfoPtr pScrn);

#endif /* OMAP_DRV_H_ */
