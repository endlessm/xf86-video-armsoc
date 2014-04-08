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

#ifndef __ARMSOC_DRV_H__
#define __ARMSOC_DRV_H__

#include "xf86.h"
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#include "xf86RAC.h"
#endif
#include "xf86drm.h"
#include <errno.h>
#include "armsoc_exa.h"

/* Apparently not used by X server */
#define ARMSOC_VERSION		1000
/* Name used to prefix messages */
#define ARMSOC_NAME			"ARMSOC"
/* Driver name as used in config file */
#define ARMSOC_DRIVER_NAME	"armsoc"

#define ARMSOC_SUPPORT_GAMMA 0

/**
 * This controls whether debug statements (and function "trace" enter/exit)
 * messages are sent to the log file (TRUE) or are ignored (FALSE).
 */
extern _X_EXPORT Bool armsocDebug;


/* Various logging/debug macros for use in the X driver and the external
 * sub-modules:
 */
#define TRACE_ENTER() \
		do { if (armsocDebug) \
			xf86DrvMsg(pScrn->scrnIndex, \
				X_INFO, "%s:%d: Entering\n",\
				__func__, __LINE__);\
		} while (0)
#define TRACE_EXIT() \
		do { if (armsocDebug) \
			xf86DrvMsg(pScrn->scrnIndex, \
				X_INFO, "%s:%d: Exiting\n",\
				__func__, __LINE__); \
		} while (0)
#define DEBUG_MSG(fmt, ...) \
		do { if (armsocDebug) \
			xf86DrvMsg(pScrn->scrnIndex, \
				X_INFO, "%s:%d " fmt "\n",\
				__func__, __LINE__, ##__VA_ARGS__); \
		} while (0)

#define INFO_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, X_INFO, fmt "\n",\
				##__VA_ARGS__); } while (0)
#define EARLY_INFO_MSG(fmt, ...) \
		do { xf86Msg(X_INFO, fmt "\n",\
				##__VA_ARGS__); } while (0)
#define CONFIG_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, fmt "\n",\
				##__VA_ARGS__); } while (0)
#define WARNING_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, \
				X_WARNING, "WARNING: " fmt "\n",\
				##__VA_ARGS__); \
		} while (0)
#define EARLY_WARNING_MSG(fmt, ...) \
		do { xf86Msg(X_WARNING, "WARNING: " fmt "\n",\
				##__VA_ARGS__); \
		} while (0)
#define ERROR_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, \
				X_ERROR, "ERROR: " fmt "\n",\
				##__VA_ARGS__); \
		} while (0)
#define EARLY_ERROR_MSG(fmt, ...) \
		do { xf86Msg(X_ERROR, "ERROR: " fmt "\n",\
				##__VA_ARGS__); \
		} while (0)

/** The driver's Screen-specific, "private" data structure. */
struct ARMSOCRec {
	/**
	 * Pointer to a structure used to communicate and coordinate with an
	 * external EXA library (if loaded).
	 */
	struct ARMSOCEXARec	*pARMSOCEXA;

	/** record if ARMSOCDRI2ScreenInit() was successful */
	Bool				dri;

	/** user-configurable option: */
	Bool				NoFlip;
	unsigned			driNumBufs;

	/** File descriptor of the connection with the DRM. */
	int					drmFD;

	char				*deviceName;

	/** interface to hardware specific functionality */
	struct drmmode_interface *drmmode_interface;

	/** DRM device instance */
	struct armsoc_device	*dev;

	/** Scan-out buffer. */
	struct armsoc_bo		*scanout;

	/** Pointer to the options for this screen. */
	OptionInfoPtr		pOptionInfo;

	/** Save (wrap) the original pScreen functions. */
	CloseScreenProcPtr				SavedCloseScreen;
	CreateScreenResourcesProcPtr	SavedCreateScreenResources;
	ScreenBlockHandlerProcPtr		SavedBlockHandler;

	/** Pointer to the entity structure for this screen. */
	EntityInfoPtr		pEntityInfo;

	/** Flips we are waiting for: */
	int					pending_flips;

	/* Identify which CRTC to use. -1 uses all CRTCs */
	int					crtcNum;

	/* The first CreatePixmap after ScreenInit ends up being the
	 * scanout, but we don't get any usage hint indicating that it should
	 * be accelerated. Use a flag to detect this and act accordingly. */
	Bool				created_scanout_pixmap;
};

/*
 * Misc utility macros:
 */

/** Return a pointer to the driver's private structure. */
#define ARMSOCPTR(p) ((struct ARMSOCRec *)((p)->driverPrivate))
#define ARMSOCPTR_FROM_SCREEN(pScreen) \
	((struct ARMSOCRec *)(xf86ScreenToScrn(pScreen))->driverPrivate)

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

/**
 * drmmode functions..
 */
Bool drmmode_pre_init(ScrnInfoPtr pScrn, int fd, int cpp);
void drmmode_screen_init(ScrnInfoPtr pScrn);
void drmmode_screen_fini(ScrnInfoPtr pScrn);
void drmmode_adjust_frame(ScrnInfoPtr pScrn, int x, int y);
Bool drmmode_page_flip(DrawablePtr draw, uint32_t fb_id, void *priv);
void drmmode_wait_for_event(ScrnInfoPtr pScrn);
Bool drmmode_cursor_init(ScreenPtr pScreen);
void drmmode_cursor_fini(ScreenPtr pScreen);


/**
 * DRI2 functions..
 */
struct ARMSOCDRISwapCmd;
Bool ARMSOCDRI2ScreenInit(ScreenPtr pScreen);
void ARMSOCDRI2CloseScreen(ScreenPtr pScreen);
void ARMSOCDRI2SwapComplete(struct ARMSOCDRISwapCmd *cmd);

/**
 * DRI2 util functions..
 */
void set_scanout_bo(ScrnInfoPtr pScrn, struct armsoc_bo *bo);

#endif /* __ARMSOC_DRV_H__ */
