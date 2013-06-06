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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "armsoc_driver.h"

#include "micmap.h"

#include "xf86cmap.h"
#include "xf86RandR12.h"

#include "compat-api.h"

#include "drmmode_driver.h"

#define DRM_DEVICE "/dev/dri/card0"

Bool armsocDebug = 0;

/*
 * Forward declarations:
 */
static const OptionInfoRec *ARMSOCAvailableOptions(int chipid, int busid);
static void ARMSOCIdentify(int flags);
static Bool ARMSOCProbe(DriverPtr drv, int flags);
static Bool ARMSOCPreInit(ScrnInfoPtr pScrn, int flags);
static Bool ARMSOCScreenInit(SCREEN_INIT_ARGS_DECL);
static void ARMSOCLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
		LOCO * colors, VisualPtr pVisual);
static Bool ARMSOCCloseScreen(CLOSE_SCREEN_ARGS_DECL);
static Bool ARMSOCCreateScreenResources(ScreenPtr pScreen);
static void ARMSOCBlockHandler(BLOCKHANDLER_ARGS_DECL);
static Bool ARMSOCSwitchMode(SWITCH_MODE_ARGS_DECL);
static void ARMSOCAdjustFrame(ADJUST_FRAME_ARGS_DECL);
static Bool ARMSOCEnterVT(VT_FUNC_ARGS_DECL);
static void ARMSOCLeaveVT(VT_FUNC_ARGS_DECL);
static void ARMSOCFreeScreen(FREE_SCREEN_ARGS_DECL);



/**
 * A structure used by the XFree86 code when loading this driver, so that it
 * can access the Probe() function, and other functions/info that it uses
 * before it calls the Probe() function.  The name of this structure must be
 * the all-upper-case version of the driver name.
 */
_X_EXPORT DriverRec ARMSOC = {
		ARMSOC_VERSION,
		(char *)ARMSOC_DRIVER_NAME,
		ARMSOCIdentify,
		ARMSOCProbe,
		ARMSOCAvailableOptions,
		NULL,
		0,
		NULL,
#ifdef XSERVER_LIBPCIACCESS
		NULL,
		NULL
#endif
};

#define MALI_4XX_CHIPSET_ID (0x0400)
#define MALI_T6XX_CHIPSET_ID (0x0600)

/** Supported "chipsets." */
static SymTabRec ARMSOCChipsets[] = {
		{ MALI_4XX_CHIPSET_ID, "Mali-4XX" },
		{ MALI_T6XX_CHIPSET_ID, "Mali-T6XX" },
		{-1, NULL }
};

/** Supported options, as enum values. */
typedef enum {
	OPTION_DEBUG,
	OPTION_NO_FLIP,
	/* TODO: MIDEGL-1453: probably need to add an option to let user specify bus-id */
} ARMSOCOpts;

/** Supported options. */
static const OptionInfoRec ARMSOCOptions[] = {
	{ OPTION_DEBUG,		"Debug",	OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_NO_FLIP,	"NoFlip",	OPTV_BOOLEAN,	{0},	FALSE },
	{ -1,				NULL,		OPTV_NONE,		{0},	FALSE }
};

/**
 * Helper functions for sharing a DRM connection across screens.
 */
static struct ARMSOCConnection {
	int fd;
	int open_count;
	int master_count;
} connection = {-1, 0, 0};

static int
ARMSOCSetDRMMaster(void)
{
	int ret=0;

	assert( connection.fd >=0 );

	if(!connection.master_count) {
		ret = drmSetMaster(connection.fd);
	}
	if(!ret) {
		connection.master_count++;
	}
	return ret;
}

static int
ARMSOCDropDRMMaster(void)
{
	int ret = 0;

	assert( connection.fd >=0 );
	assert( connection.master_count > 0 );

	if(1 == connection.master_count) {
		ret = drmDropMaster(connection.fd);
	}
	if(!ret) {
		connection.master_count--;
	}
	return ret;
}

static Bool
ARMSOCOpenDRM(ScrnInfoPtr pScrn)
{
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);
	drmSetVersion sv;
	int err;

	if(-1 == connection.fd)
	{
		assert(!connection.open_count);
		assert(!connection.master_count);
		pARMSOC->drmFD = open(DRM_DEVICE, O_RDWR, 0);
		if (pARMSOC->drmFD == -1) {
			ERROR_MSG("Cannot open a connection with the DRM.");
			return FALSE;
		}
		/* Check that what we are or can become drm master by attempting
		 * a drmSetInterfaceVersion(). If successful this leaves us as master.
		 * (see DRIOpenDRMMaster() in DRI1)
		 */
		sv.drm_di_major = 1;
		sv.drm_di_minor = 1;
		sv.drm_dd_major = -1;
		sv.drm_dd_minor = -1;
		err = drmSetInterfaceVersion(pARMSOC->drmFD, &sv);
		if (err != 0) {
			ERROR_MSG("Cannot set the DRM interface version.");
			drmClose(pARMSOC->drmFD);
			pARMSOC->drmFD = -1;
			return FALSE;
		}
		connection.fd = pARMSOC->drmFD;
		connection.open_count = 1;
		connection.master_count = 1;
	}
	else
	{
		assert(connection.open_count);
		connection.open_count++;
		connection.master_count++;
		pARMSOC->drmFD = connection.fd;
	}
	pARMSOC->deviceName = drmGetDeviceNameFromFd(pARMSOC->drmFD);

	return TRUE;
}

/**
 * Helper function for closing a connection to the DRM.
 */
static void
ARMSOCCloseDRM(ScrnInfoPtr pScrn)
{
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);

	if (pARMSOC && (pARMSOC->drmFD >= 0)) {
		drmFree(pARMSOC->deviceName);
		connection.open_count--;
		if( !connection.open_count )
		{
			assert(!connection.master_count);
			drmClose(pARMSOC->drmFD);
			connection.fd = -1;
		}
		pARMSOC->drmFD = -1;
	}
}

/** Let the XFree86 code know the Setup() function. */
static MODULESETUPPROTO(ARMSOCSetup);

/** Provide basic version information to the XFree86 code. */
static XF86ModuleVersionInfo ARMSOCVersRec =
{
		ARMSOC_DRIVER_NAME,
		MODULEVENDORSTRING,
		MODINFOSTRING1,
		MODINFOSTRING2,
		XORG_VERSION_CURRENT,
		PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
		ABI_CLASS_VIDEODRV,
		ABI_VIDEODRV_VERSION,
		MOD_CLASS_VIDEODRV,
		{0, 0, 0, 0}
};

/** Let the XFree86 code know about the VersRec and Setup() function. */
_X_EXPORT XF86ModuleData armsocModuleData = { &ARMSOCVersRec, ARMSOCSetup, NULL };


/**
 * The first function that the XFree86 code calls, after loading this module.
 */
static pointer
ARMSOCSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	static Bool setupDone = FALSE;

	/* This module should be loaded only once, but check to be sure: */
	if (!setupDone) {
		setupDone = TRUE;
		xf86AddDriver(&ARMSOC, module, 0);

		/* The return value must be non-NULL on success even though there is no
		 * TearDownProc.
		 */
		return (pointer) 1;
	} else {
		if (errmaj)
			*errmaj = LDR_ONCEONLY;
		return NULL;
	}
}

/**
 * The mandatory AvailableOptions() function.  It returns the available driver
 * options to the "-configure" option, so that an xorg.conf file can be built
 * and the user can see which options are available for them to use.
 */
static const OptionInfoRec *
ARMSOCAvailableOptions(int chipid, int busid)
{
	return ARMSOCOptions;
}



/**
 * The mandatory Identify() function.  It is run before Probe(), and prints out
 * an identifying message, which includes the chipset(s) the driver supports.
 */
static void
ARMSOCIdentify(int flags)
{
	xf86PrintChipsets(ARMSOC_NAME, "Driver for ARM compatible chipsets",
			ARMSOCChipsets);
}



/**
 * The driver's Probe() function.  This function finds all instances of
 * ARM hardware that the driver supports (from within the "xorg.conf"
 * device sections), and for instances not already claimed by another driver,
 * claim the instances, and allocate a ScrnInfoRec.  Only minimal hardware
 * probing is allowed here.
 */
static Bool
ARMSOCProbe(DriverPtr drv, int flags)
{
	int i;
	ScrnInfoPtr pScrn;
	GDevPtr *devSections=NULL;
	int numDevSections;
	Bool foundScreen = FALSE;

	/* Get the "xorg.conf" file device sections that match this driver, and
	 * return (error out) if there are none:
	 */
	numDevSections = xf86MatchDevice(ARMSOC_DRIVER_NAME, &devSections);
	if (numDevSections <= 0) {
		EARLY_ERROR_MSG("Did not find any matching device section in "
				"configuration file");
		if (flags & PROBE_DETECT) {
			/* if we are probing, assume one and lets see if we can
			 * open the device to confirm it is there:
			 */
			numDevSections = 1;
		} else {
			return FALSE;
		}
	}

	for (i = 0; i < numDevSections; i++) {
		int fd = open(DRM_DEVICE, O_RDWR, 0);
		if (fd != -1) {
			ARMSOCPtr pARMSOC;

			/* Allocate the ScrnInfoRec */
			pScrn = xf86AllocateScreen(drv, 0);
			if (!pScrn) {
				EARLY_ERROR_MSG("Cannot allocate a ScrnInfoPtr");
				return FALSE;
			}
			/* Allocate the driver's Screen-specific, "private" data structure
			 * and hook it into the ScrnInfoRec's driverPrivate field. */
			pScrn->driverPrivate = calloc(1, sizeof(ARMSOCRec));
			if (!pScrn->driverPrivate)
				return FALSE;

			pARMSOC = ARMSOCPTR(pScrn);
			pARMSOC->crtcNum = -1; /* intially mark to use all DRM crtc */

			if (flags & PROBE_DETECT) {
				/* just add the device.. we aren't a PCI device, so
				 * call xf86AddBusDeviceToConfigure() directly
				 */
				xf86AddBusDeviceToConfigure(ARMSOC_DRIVER_NAME,
						BUS_NONE, NULL, i);
				foundScreen = TRUE;
				drmClose(fd);
				continue;
			}

			if (devSections) {
				int entity = xf86ClaimNoSlot(drv, 0, devSections[i], TRUE);
				xf86AddEntityToScreen(pScrn, entity);
			}

			/* if there are multiple screens, use a separate crtc for each one */
			if(numDevSections > 1) {
				pARMSOC->crtcNum = i;
			}

			xf86Msg(X_INFO,"Screen:%d,  CRTC:%d\n", pScrn->scrnIndex, pARMSOC->crtcNum);

			foundScreen = TRUE;

			pScrn->driverVersion = ARMSOC_VERSION;
			pScrn->driverName    = (char *)ARMSOC_DRIVER_NAME;
			pScrn->name          = (char *)ARMSOC_NAME;
			pScrn->Probe         = ARMSOCProbe;
			pScrn->PreInit       = ARMSOCPreInit;
			pScrn->ScreenInit    = ARMSOCScreenInit;
			pScrn->SwitchMode    = ARMSOCSwitchMode;
			pScrn->AdjustFrame   = ARMSOCAdjustFrame;
			pScrn->EnterVT       = ARMSOCEnterVT;
			pScrn->LeaveVT       = ARMSOCLeaveVT;
			pScrn->FreeScreen    = ARMSOCFreeScreen;

			/* would be nice to keep the connection open */
			drmClose(fd);
		}
	}
	free(devSections);
	return foundScreen;
}

/**
 * The driver's PreInit() function.  Additional hardware probing is allowed
 * now, including display configuration.
 */
static Bool
ARMSOCPreInit(ScrnInfoPtr pScrn, int flags)
{
	ARMSOCPtr pARMSOC;
	int default_depth, fbbpp;
	rgb defaultWeight = { 0, 0, 0 };
	rgb defaultMask = { 0, 0, 0 };
	Gamma defaultGamma = { 0.0, 0.0, 0.0 };
	int i;

	TRACE_ENTER();

	if (flags & PROBE_DETECT) {
		ERROR_MSG("The %s driver does not support the \"-configure\" or "
				"\"-probe\" command line arguments.", ARMSOC_NAME);
		return FALSE;
	}

	/* Check the number of entities, and fail if it isn't one. */
	if (pScrn->numEntities != 1) {
		ERROR_MSG("Driver expected 1 entity, but found %d for screen %d",
				pScrn->numEntities, pScrn->scrnIndex);
		return FALSE;
	}

	pARMSOC = ARMSOCPTR(pScrn);
	pARMSOC->pEntityInfo = xf86GetEntityInfo(pScrn->entityList[0]);

	pScrn->monitor = pScrn->confScreen->monitor;

	/* Get the current depth, and set it for XFree86: */
	default_depth = 24;  /* TODO: MIDEGL-1445: get from kernel */
	fbbpp = 32;          /* TODO: MIDEGL-1445: get from kernel */

	if (!xf86SetDepthBpp(pScrn, default_depth, 0, fbbpp, Support32bppFb)) {
		/* The above function prints an error message. */
		goto fail;
	}
	xf86PrintDepthBpp(pScrn);

	/* Set the color weight: */
	if (!xf86SetWeight(pScrn, defaultWeight, defaultMask)) {
		/* The above function prints an error message. */
		goto fail;
	}

	/* Set the gamma: */
	if (!xf86SetGamma(pScrn, defaultGamma)) {
		/* The above function prints an error message. */
		goto fail;
	}

	/* Visual init: */
	if (!xf86SetDefaultVisual(pScrn, -1)) {
		/* The above function prints an error message. */
		goto fail;
	}

	/* We don't support 8-bit depths: */
	if (pScrn->depth < 16) {
		ERROR_MSG("The requested default visual (%s) has an unsupported "
				"depth (%d).",
				xf86GetVisualName(pScrn->defaultVisual), pScrn->depth);
		goto fail;
	}

	/* Using a programmable clock: */
	pScrn->progClock = TRUE;

	/* Open a connection to the DRM, so we can communicate with the KMS code: */
	if (!ARMSOCOpenDRM(pScrn)) {
		goto fail;
	}

	pARMSOC->drmmode_interface = drmmode_interface_get_implementation(pARMSOC->drmFD);
	if (!pARMSOC->drmmode_interface)
		goto fail2;

	/* create DRM device instance: */
	pARMSOC->dev = armsoc_device_new(pARMSOC->drmFD,
			pARMSOC->drmmode_interface->dumb_scanout_flags,
			pARMSOC->drmmode_interface->dumb_no_scanout_flags);

	/* find matching chipset name: */
	for (i = 0; ARMSOCChipsets[i].name; i++) {
		if (ARMSOCChipsets[i].token == MALI_T6XX_CHIPSET_ID) {
			pScrn->chipset = (char *)ARMSOCChipsets[i].name;
			break;
		}
	}

	INFO_MSG("Chipset: %s", pScrn->chipset);

	/*
	 * Process the "xorg.conf" file options:
	 */
	xf86CollectOptions(pScrn, NULL);
	if (!(pARMSOC->pOptionInfo = calloc(1, sizeof(ARMSOCOptions))))
		goto fail2;
	memcpy(pARMSOC->pOptionInfo, ARMSOCOptions, sizeof(ARMSOCOptions));
	xf86ProcessOptions(pScrn->scrnIndex, pARMSOC->pEntityInfo->device->options,
			pARMSOC->pOptionInfo);

	/* Determine if the user wants debug messages turned on: */
	armsocDebug = xf86ReturnOptValBool(pARMSOC->pOptionInfo, OPTION_DEBUG, FALSE);

	/* Determine if user wants to disable buffer flipping: */
	pARMSOC->NoFlip = xf86ReturnOptValBool(pARMSOC->pOptionInfo,
			OPTION_NO_FLIP, FALSE);
	INFO_MSG("Buffer Flipping is %s", pARMSOC->NoFlip ? "Disabled" : "Enabled");

	/*
	 * Select the video modes:
	 */
	INFO_MSG("Setting the video modes ...");

	/* Don't call drmCheckModesettingSupported() as its written only for
	 * PCI devices.
	 */

	/* Do initial KMS setup: */
	if (!drmmode_pre_init(pScrn, pARMSOC->drmFD, (pScrn->bitsPerPixel >> 3))) {
		ERROR_MSG("Cannot get KMS resources");
		goto fail2;
	} else {
		INFO_MSG("Got KMS resources");
	}

	xf86RandR12PreInit(pScrn);

	/* Let XFree86 calculate or get (from command line) the display DPI: */
	xf86SetDpi(pScrn, 0, 0);

	/* Ensure we have a supported depth: */
	switch (pScrn->bitsPerPixel) {
	case 16:
	case 24:
	case 32:
		break;
	default:
		ERROR_MSG("The requested number of bits per pixel (%d) is unsupported.",
				pScrn->bitsPerPixel);
		goto fail2;
	}


	/* Load external sub-modules now: */

	if (!(xf86LoadSubModule(pScrn, "dri2") &&
			xf86LoadSubModule(pScrn, "exa") &&
			xf86LoadSubModule(pScrn, "fb"))) {
		goto fail2;
	}

	TRACE_EXIT();
	return TRUE;

fail2:
	/* Cleanup here where we know whether we took a connection
	 * instead of in FreeScreen where we don't */
	ARMSOCDropDRMMaster();
	ARMSOCCloseDRM(pScrn);
fail:
	TRACE_EXIT();
	return FALSE;
}


/**
 * Initialize EXA and DRI2
 */
static void
ARMSOCAccelInit(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);

	if (!pARMSOC->pARMSOCEXA) {
		pARMSOC->pARMSOCEXA = InitNullEXA(pScreen, pScrn, pARMSOC->drmFD);
	}

	if (pARMSOC->pARMSOCEXA) {
		pARMSOC->dri = ARMSOCDRI2ScreenInit(pScreen);
	} else {
		pARMSOC->dri = FALSE;
	}
}

/**
 * The driver's ScreenInit() function, called at the start of each server
 * generation. Fill in pScreen, map the frame buffer, save state, 
 * initialize the mode, etc.
 */
static Bool
ARMSOCScreenInit(SCREEN_INIT_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);
	VisualPtr visual;
	xf86CrtcConfigPtr xf86_config;
	int j;

	TRACE_ENTER();

	/* set drm master before allocating scanout buffer */
	if(ARMSOCSetDRMMaster()) {
		ERROR_MSG("Cannot get DRM master: %s", strerror(errno));
		goto fail;
	}
	/* Allocate initial scanout buffer */
	DEBUG_MSG("allocating new scanout buffer: %dx%d",
			pScrn->virtualX, pScrn->virtualY);
	assert(!pARMSOC->scanout);
	pARMSOC->scanout = armsoc_bo_new_with_dim(pARMSOC->dev, pScrn->virtualX,
			pScrn->virtualY, pScrn->depth, pScrn->bitsPerPixel,
			ARMSOC_BO_SCANOUT );
	if (!pARMSOC->scanout) {
		ERROR_MSG("Cannot allocate scanout buffer\n");
		goto fail1;
	}
	pScrn->displayWidth = armsoc_bo_pitch(pARMSOC->scanout) / ((pScrn->bitsPerPixel+7) / 8);
	xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);

	/* need to point to new screen on server regeneration */
	for (j = 0; j < xf86_config->num_crtc; j++)
		xf86_config->crtc[j]->scrn = pScrn;
	for (j = 0; j < xf86_config->num_output; j++)
		xf86_config->output[j]->scrn = pScrn;

	/*
	 * The next step is to setup the screen's visuals, and initialize the
	 * framebuffer code.  In cases where the framebuffer's default
	 * choices for things like visual layouts and bits per RGB are OK,
	 * this may be as simple as calling the framebuffer's ScreenInit()
	 * function.  If not, the visuals will need to be setup before calling
	 * a fb ScreenInit() function and fixed up after.
	 *
	 * For most PC hardware at depths >= 8, the defaults that fb uses
	 * are not appropriate.  In this driver, we fixup the visuals after.
	 */

	/* Reset the visual list. */
	miClearVisualTypes();
	if (!miSetVisualTypes(pScrn->bitsPerPixel, miGetDefaultVisualMask(pScrn->depth),
			pScrn->rgbBits, pScrn->defaultVisual)) {
		ERROR_MSG("Cannot initialize the visual type for %d bits per pixel!",
				pScrn->bitsPerPixel);
		goto fail2;
	}

	if(pScrn->bitsPerPixel == 32 && pScrn->depth == 24) {
		/* Also add a 24 bit depth visual */
		if (!miSetVisualTypes(24, miGetDefaultVisualMask(pScrn->depth),
				pScrn->rgbBits, pScrn->defaultVisual)) {
			WARNING_MSG("Cannot initialize a 24 depth visual for 32bpp");
		}else{
			INFO_MSG("Initialized a 24 depth visual for 32bpp");
		}
	}

	if (!miSetPixmapDepths()) {
		ERROR_MSG("Cannot initialize the pixmap depth!");
		goto fail3;
	}

	/* Initialize some generic 2D drawing functions: */
	if (!fbScreenInit(pScreen, armsoc_bo_map(pARMSOC->scanout),
			pScrn->virtualX, pScrn->virtualY,
			pScrn->xDpi, pScrn->yDpi, pScrn->displayWidth,
			pScrn->bitsPerPixel)) {
		ERROR_MSG("fbScreenInit() failed!");
		goto fail3;
	}

	/* Fixup RGB ordering: */
	visual = pScreen->visuals + pScreen->numVisuals;
	while (--visual >= pScreen->visuals) {
		if ((visual->class | DynamicClass) == DirectColor) {
			visual->offsetRed = pScrn->offset.red;
			visual->offsetGreen = pScrn->offset.green;
			visual->offsetBlue = pScrn->offset.blue;
			visual->redMask = pScrn->mask.red;
			visual->greenMask = pScrn->mask.green;
			visual->blueMask = pScrn->mask.blue;
			visual->bitsPerRGBValue = pScrn->rgbBits;
			visual->ColormapEntries = 1 << pScrn->rgbBits;
		}
	}

	/* Continue initializing the generic 2D drawing functions after fixing the
	 * RGB ordering:
	 */
	if (!fbPictureInit(pScreen, NULL, 0)) {
		ERROR_MSG("fbPictureInit() failed!");
		goto fail4;
	}

	/* Set the initial black & white colormap indices: */
	xf86SetBlackWhitePixels(pScreen);

	/* Initialize external sub-modules for EXA now, this has to be before
	 * miDCInitialize() otherwise stacking order for wrapped ScreenPtr fxns
	 * ends up in the wrong order.
	 */
	ARMSOCAccelInit(pScreen);

	/* Initialize backing store: */
	xf86SetBackingStore(pScreen);

	/* Cause the cursor position to be updated by the mouse signal handler: */
	xf86SetSilkenMouse(pScreen);

	/* Initialize the cursor: */
	if(!miDCInitialize(pScreen, xf86GetPointerScreenFuncs())) {
		ERROR_MSG("miDCInitialize() failed!");
		goto fail5;
	}

	/* ignore failures here as we will fall back to software cursor */
	(void)drmmode_cursor_init(pScreen);

	/* TODO: MIDEGL-1458: Is this the right place for this?
	 * The Intel i830 driver says:
	 * "Must force it before EnterVT, so we are in control of VT..."
	 */
	pScrn->vtSema = TRUE;

	/* Take over the virtual terminal from the console, set the desired mode,
	 * etc.:
	 */
	if (!ARMSOCEnterVT(VT_FUNC_ARGS(0))) {
		ERROR_MSG("ARMSOCEnterVT() failed!");
		goto fail6;
	}

	/* Do some XRandR initialization. Return value is not useful */
	(void)xf86CrtcScreenInit(pScreen);

	if (!miCreateDefColormap(pScreen)) {
		ERROR_MSG("Cannot create colormap!");
		goto fail7;
	}

	if (!xf86HandleColormaps(pScreen, 1 << pScrn->rgbBits, pScrn->rgbBits,
			ARMSOCLoadPalette, NULL, CMAP_PALETTED_TRUECOLOR)) {
		ERROR_MSG("xf86HandleColormaps() failed!");
		goto fail8;
	}

	/* Setup power management: */
	xf86DPMSInit(pScreen, xf86DPMSSet, 0);

	pScreen->SaveScreen = xf86SaveScreen;

	/* Wrap some screen functions: */
	wrap(pARMSOC, pScreen, CloseScreen, ARMSOCCloseScreen);
	wrap(pARMSOC, pScreen, CreateScreenResources, ARMSOCCreateScreenResources);
	wrap(pARMSOC, pScreen, BlockHandler, ARMSOCBlockHandler);
	drmmode_screen_init(pScrn);

	TRACE_EXIT();
	return TRUE;

/* cleanup on failures */
fail8:
	/* uninstall the default colormap */
	miUninstallColormap(GetInstalledmiColormap(pScreen));

fail7:
	ARMSOCLeaveVT(VT_FUNC_ARGS(0));
	pScrn->vtSema = FALSE;

fail6:
	drmmode_cursor_fini(pScreen);

fail5:
	if (pARMSOC->dri) {
		ARMSOCDRI2CloseScreen(pScreen);
	}
	if (pARMSOC->pARMSOCEXA) {
		if (pARMSOC->pARMSOCEXA->CloseScreen) {
			pARMSOC->pARMSOCEXA->CloseScreen(pScrn->scrnIndex, pScreen);
		}
	}
fail4:
	/* Call the CloseScreen functions for fbInitScreen,  miDCInitialize,
	 * exaDriverInit & xf86CrtcScreenInit as appropriate via their wrapped pointers.
	 * exaDDXCloseScreen uses the XF86SCRNINFO macro so we must
	 * set up the key for this before it gets called.
	 */
	dixSetPrivate(&pScreen->devPrivates, xf86ScreenKey, pScrn);
	(*pScreen->CloseScreen)(pScrn->scrnIndex, pScreen);

fail3:
	/* reset the visual list */
	miClearVisualTypes();

fail2:
	/* release the scanout buffer */
	armsoc_bo_unreference(pARMSOC->scanout);
	pARMSOC->scanout = NULL;
	pScrn->displayWidth = 0;

fail1:
	/* drop drm master */
	(void)drmDropMaster(pARMSOC->drmFD);

fail:
	TRACE_EXIT();
	return FALSE;
}


static void
ARMSOCLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
		LOCO * colors, VisualPtr pVisual)
{
	TRACE_ENTER();
	TRACE_EXIT();
}


/**
 * The driver's CloseScreen() function.  This is called at the end of each
 * server generation.  Restore state, unmap the frame buffer (and any other
 * mapped memory regions), and free per-Screen data structures (except those
 * held by pScrn).
 */
static Bool
ARMSOCCloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);

	TRACE_ENTER();

	drmmode_screen_fini(pScrn);

	if (pARMSOC->dri) {
		ARMSOCDRI2CloseScreen(pScreen);
	}
	if (pARMSOC->pARMSOCEXA) {
		if (pARMSOC->pARMSOCEXA->CloseScreen) {
			pARMSOC->pARMSOCEXA->CloseScreen(CLOSE_SCREEN_ARGS);
		}
	}

	/* release the scanout buffer */
	armsoc_bo_unreference(pARMSOC->scanout);
	pARMSOC->scanout = NULL;
	pScrn->displayWidth = 0;

	if (pScrn->vtSema == TRUE) {
		ARMSOCLeaveVT(VT_FUNC_ARGS(0));
	}
	pScrn->vtSema = FALSE;

	unwrap(pARMSOC, pScreen, CloseScreen);
	unwrap(pARMSOC, pScreen, BlockHandler);
	unwrap(pARMSOC, pScreen, CreateScreenResources);

	TRACE_EXIT();

	return (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
}



/**
 * Adjust the screen pixmap for the current location of the front buffer.
 * This is done at EnterVT when buffers are bound as long as the resources
 * have already been created, but the first EnterVT happens before
 * CreateScreenResources.
 */
static Bool
ARMSOCCreateScreenResources(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);

	swap(pARMSOC, pScreen, CreateScreenResources);
	if (!(*pScreen->CreateScreenResources) (pScreen))
		return FALSE;
	swap(pARMSOC, pScreen, CreateScreenResources);

	return TRUE;
}


static void
ARMSOCBlockHandler(BLOCKHANDLER_ARGS_DECL)
{
	SCREEN_PTR(arg);
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);

	swap(pARMSOC, pScreen, BlockHandler);
	(*pScreen->BlockHandler) (BLOCKHANDLER_ARGS);
	swap(pARMSOC, pScreen, BlockHandler);
}



/**
 * The driver's SwitchMode() function.  Initialize the new mode for the
 * Screen.
 */
static Bool
ARMSOCSwitchMode(SWITCH_MODE_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	return xf86SetSingleMode(pScrn, mode, RR_Rotate_0);
}



/**
 * The driver's AdjustFrame() function.  For cases where the frame buffer is
 * larger than the monitor resolution, this function can pan around the frame
 * buffer within the "viewport" of the monitor.
 */
static void
ARMSOCAdjustFrame(ADJUST_FRAME_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	drmmode_adjust_frame(pScrn, x, y);
}



/**
 * The driver's EnterVT() function.  This is called at server startup time, and
 * when the X server takes over the virtual terminal from the console.  As
 * such, it may need to save the current (i.e. console) HW state, and set the
 * HW state as needed by the X server.
 */
static Bool
ARMSOCEnterVT(VT_FUNC_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	int i, ret;

	TRACE_ENTER();

	for (i = 1; i < currentMaxClients; i++) {
		if (clients[i])
			AttendClient(clients[i]);
	}

	ret = ARMSOCSetDRMMaster();
	if (ret) {
		ERROR_MSG("Cannot get DRM master: %s", strerror(errno));
		return FALSE;
	}

	if (!xf86SetDesiredModes(pScrn)) {
		ERROR_MSG("xf86SetDesiredModes() failed!");
		return FALSE;
	}

	TRACE_EXIT();
	return TRUE;
}



/**
 * The driver's LeaveVT() function.  This is called when the X server
 * temporarily gives up the virtual terminal to the console.  As such, it may
 * need to restore the console's HW state.
 */
static void
ARMSOCLeaveVT(VT_FUNC_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	int i, ret;

	TRACE_ENTER();

	for (i = 1; i < currentMaxClients; i++) {
		if (clients[i])
			IgnoreClient(clients[i]);
	}

	ret = ARMSOCDropDRMMaster();
	if (ret) {
		WARNING_MSG("drmDropMaster failed: %s", strerror(errno));
	}

	TRACE_EXIT();
}

/**
 * The driver's FreeScreen() function.
 * Frees the ScrnInfoRec driverPrivate field when a screen is deleted by the common layer.
 * This function is not used in normal (error free) operation.
 */
static void
ARMSOCFreeScreen(FREE_SCREEN_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	ARMSOCPtr pARMSOC = ARMSOCPTR(pScrn);

	TRACE_ENTER();

	if (!pARMSOC) {
		/* This can happen if a Screen is deleted after Probe(): */
		return;
	}

	if (pARMSOC->pARMSOCEXA) {
		if (pARMSOC->pARMSOCEXA->FreeScreen) {
			pARMSOC->pARMSOCEXA->FreeScreen(FREE_SCREEN_ARGS(pScrn));
		}
	}

	armsoc_device_del(pARMSOC->dev);

	/* Free the driver's Screen-specific, "private" data structure and
	 * NULL-out the ScrnInfoRec's driverPrivate field.
	 */
	if (pScrn->driverPrivate) {
		free(pScrn->driverPrivate);
		pScrn->driverPrivate = NULL;
	}

	TRACE_EXIT();
}

