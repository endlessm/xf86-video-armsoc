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
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <pixman.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "armsoc_driver.h"

#include "micmap.h"

#include "xf86cmap.h"
#include "xf86RandR12.h"
#include "xf86drmMode.h"

#include "compat-api.h"

#include "drmmode_driver.h"

#define DRM_DEVICE "/dev/dri/card%d"

Bool armsocDebug;

/*
 * Forward declarations:
 */
static const OptionInfoRec *ARMSOCAvailableOptions(int chipid, int busid);
static void ARMSOCIdentify(int flags);
static Bool ARMSOCProbe(DriverPtr drv, int flags);
static Bool ARMSOCPreInit(ScrnInfoPtr pScrn, int flags);
static Bool ARMSOCScreenInit(SCREEN_INIT_ARGS_DECL);
static void ARMSOCLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
		LOCO *colors, VisualPtr pVisual);
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

/** Supported "chipsets." */
#define ARMSOC_CHIPSET_NAME "Mali"

/** Supported options, as enum values. */
enum {
	OPTION_DEBUG,
	OPTION_NO_FLIP,
	OPTION_CARD_NUM,
	OPTION_BUSID,
	OPTION_DRIVERNAME,
	OPTION_DRI_NUM_BUF,
};

/** Supported options. */
static const OptionInfoRec ARMSOCOptions[] = {
	{ OPTION_DEBUG,      "Debug",      OPTV_BOOLEAN, {0}, FALSE },
	{ OPTION_NO_FLIP,    "NoFlip",     OPTV_BOOLEAN, {0}, FALSE },
	{ OPTION_CARD_NUM,   "DRICard",    OPTV_INTEGER, {0}, FALSE },
	{ OPTION_BUSID,      "BusID",      OPTV_STRING,  {0}, FALSE },
	{ OPTION_DRIVERNAME, "DriverName", OPTV_STRING,  {0}, FALSE },
	{ OPTION_DRI_NUM_BUF, "DRI2MaxBuffers", OPTV_INTEGER, {-1}, FALSE },
	{ -1,                NULL,         OPTV_NONE,    {0}, FALSE }
};

/**
 * Helper functions for sharing a DRM connection across screens.
 */
static struct ARMSOCConnection {
	const char *driver_name;
	const char *bus_id;
	unsigned int card_num;
	int fd;
	int open_count;
	int master_count;
} connection = {NULL, NULL, 0, -1, 0, 0};

static int
ARMSOCSetDRMMaster(void)
{
	int ret = 0;

	assert(connection.fd >= 0);

	if (!connection.master_count)
		ret = drmSetMaster(connection.fd);

	if (!ret)
		connection.master_count++;

	return ret;
}

static int
ARMSOCDropDRMMaster(void)
{
	int ret = 0;

	assert(connection.fd >= 0);
	assert(connection.master_count > 0);

	if (1 == connection.master_count)
		ret = drmDropMaster(connection.fd);

	if (!ret)
		connection.master_count--;

	return ret;
}

static void
ARMSOCShowDriverInfo(int fd)
{
	char *bus_id;
	drmVersionPtr version;
	char *deviceName;

	EARLY_INFO_MSG("Opened DRM");
	deviceName = drmGetDeviceNameFromFd(fd);
	EARLY_INFO_MSG("   DeviceName is [%s]",
			deviceName ? deviceName : "NULL");
	drmFree(deviceName);
	bus_id = drmGetBusid(fd);
	EARLY_INFO_MSG("   bus_id is [%s]",
			bus_id ? bus_id : "NULL");
	drmFreeBusid(bus_id);
	version = drmGetVersion(fd);
	if (version) {
		EARLY_INFO_MSG("   DriverName is [%s]",
					version->name);
		EARLY_INFO_MSG("   version is [%d.%d.%d]",
					version->version_major,
					version->version_minor,
					version->version_patchlevel);
		drmFreeVersion(version);
	} else {
		EARLY_INFO_MSG("   version is [NULL]");
	}
	return;
}

static int
ARMSOCOpenDRMCard(void)
{
	int fd;

	if ((connection.bus_id) || (connection.driver_name)) {
		/* user specified bus ID or driver name - pass to drmOpen */
		EARLY_INFO_MSG("Opening driver [%s], bus_id [%s]",
			connection.driver_name ?
					connection.driver_name : "NULL",
			connection.bus_id ?
					connection.bus_id : "NULL");
		fd = drmOpen(connection.driver_name, connection.bus_id);
		if (fd < 0)
			goto fail2;
	} else {
		char filename[32];
		int err;
		drmSetVersion sv;
		char *bus_id, *bus_id_copy;

		/* open with card_num */
		snprintf(filename, sizeof(filename),
				DRM_DEVICE, connection.card_num);
		EARLY_INFO_MSG(
				"No BusID or DriverName specified - opening %s",
				filename);
		fd = open(filename, O_RDWR, 0);
		if (-1 == fd)
			goto fail2;
		/* Set interface version to initialise bus id */
		sv.drm_di_major = 1;
		sv.drm_di_minor = 1;
		sv.drm_dd_major = -1;
		sv.drm_dd_minor = -1;
		err = drmSetInterfaceVersion(fd, &sv);
		if (err) {
			EARLY_ERROR_MSG(
				"Cannot set the DRM interface version.");
			goto fail1;
		}
		/* get the bus id */
		bus_id = drmGetBusid(fd);
		if (!bus_id) {
			EARLY_ERROR_MSG("Couldn't get BusID from %s",
							filename);
			goto fail1;
		}
		EARLY_INFO_MSG("Got BusID %s", bus_id);
		bus_id_copy = malloc(strlen(bus_id)+1);
		if (!bus_id_copy) {
			EARLY_ERROR_MSG("Memory alloc failed");
			goto fail1;
		}
		strcpy(bus_id_copy, bus_id);
		drmFreeBusid(bus_id);
		err = close(fd);
		if (err) {
			free(bus_id_copy);
			EARLY_ERROR_MSG("Couldn't close %s", filename);
			goto fail2;
		}
		/* use bus_id to open driver */
		fd = drmOpen(NULL, bus_id_copy);
		free(bus_id_copy);
		if (fd < 0)
			goto fail2;
	}
	ARMSOCShowDriverInfo(fd);
	return fd;

fail1:
	close(fd);
fail2:
	EARLY_ERROR_MSG(
		"Cannot open a connection with the DRM - %s",
		strerror(errno));
	return -1;
}

static Bool
ARMSOCOpenDRM(ScrnInfoPtr pScrn)
{
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	drmSetVersion sv;
	int err;

	if (connection.fd < 0) {
		assert(!connection.open_count);
		assert(!connection.master_count);
		pARMSOC->drmFD = ARMSOCOpenDRMCard();
		if (pARMSOC->drmFD < 0)
			return FALSE;
		/* Check that what we are or can become drm master by
		 * attempting a drmSetInterfaceVersion(). If successful
		 * this leaves us as master.
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
	} else {
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
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	if (pARMSOC && (pARMSOC->drmFD >= 0)) {
		drmFree(pARMSOC->deviceName);
		connection.open_count--;
		if (!connection.open_count) {
			assert(!connection.master_count);
			drmClose(pARMSOC->drmFD);
			connection.fd = -1;
		}
		pARMSOC->drmFD = -1;
	}
}

static Bool ARMSOCCopyDRMFB(ScrnInfoPtr pScrn)
{
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	drmModeCrtcPtr crtc;
	drmModeFBPtr fb;
	struct drm_mode_map_dumb mreq;
	uint32_t crct_id;
	uint32_t src_pitch;
	int src_cpp;
	int dst_width, dst_height, dst_bpp, dst_pitch;
	int width, height;
	int src_pixman_stride, dst_pixman_stride;
	Bool ret = FALSE;
	unsigned char *src = NULL, *dst = NULL;
	unsigned int src_size = 0;
	pixman_bool_t pixman_ret;

	crct_id = drmmode_get_crtc_id(pScrn);

	crtc = drmModeGetCrtc(pARMSOC->drmFD, crct_id);
	if (!crtc) {
		ERROR_MSG("Couldn't get crtc");
		goto exit;
	}

	fb = drmModeGetFB(pARMSOC->drmFD, crtc->buffer_id);
	if (!fb) {
		ERROR_MSG("Couldn't get fb");
		goto free_crtc;
	}

	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = fb->handle;
	ret = drmIoctl(pARMSOC->drmFD, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	if (ret) {
		ERROR_MSG("DRM_IOCTL_MODE_MAP_DUMB ioctl failed");
		goto free_fb;
	}

	dst = armsoc_bo_map(pARMSOC->scanout);
	if (!dst) {
		ERROR_MSG("Couldn't map scanout bo");
		goto free_fb;
	}

	src_cpp = (fb->bpp + 7) / 8;
	src_pitch = fb->width * src_cpp;
	src_size = fb->height * src_pitch;

	src = mmap(0, src_size, PROT_READ | PROT_WRITE, MAP_SHARED, pARMSOC->drmFD, mreq.offset);
	if (src == MAP_FAILED) {
		ERROR_MSG("Couldn't mmap");
		goto free_fb;
	}

	dst_width = armsoc_bo_width(pARMSOC->scanout);
	dst_height = armsoc_bo_height(pARMSOC->scanout);
	dst_bpp = armsoc_bo_bpp(pARMSOC->scanout);
	dst_pitch = armsoc_bo_pitch(pARMSOC->scanout);

	width = min(fb->width, dst_width);
	height = min(fb->height, dst_height);

	src_pixman_stride = src_pitch/sizeof(uint32_t);
	dst_pixman_stride = dst_pitch/sizeof(uint32_t);

	if (src_pitch % sizeof(uint32_t) || dst_pitch % sizeof(uint32_t)) {
		ERROR_MSG("Buffer strides need to be a multiple of 4 bytes");
		goto free_fb;
	}

	pixman_ret = pixman_blt((uint32_t *)src, (uint32_t *)dst,
			src_pixman_stride, dst_pixman_stride,
			fb->bpp, dst_bpp, crtc->x,
			crtc->y, 0, 0, width, height);
	if (!pixman_ret) {
		ERROR_MSG("Pixman failed to blit to scanout buffer");
		goto free_fb;
	}

	if (width < dst_width) {
		pixman_ret = pixman_fill((uint32_t *)dst, dst_pixman_stride,
				dst_bpp, width, 0, dst_width-width, dst_height, 0);
		if (!pixman_ret) {
			ERROR_MSG("Pixman failed to fill margin of scanout buffer");
			goto free_fb;
		}
	}

	if (height < dst_height) {
		pixman_ret = pixman_fill((uint32_t *)dst, dst_pixman_stride,
				dst_bpp, 0, height, width, dst_height-height, 0);
		if (!pixman_ret) {
			ERROR_MSG("Pixman failed to fill margin of scanout buffer");
			goto free_fb;
		}
	}

	ret = TRUE;

free_fb:
	drmModeFreeFB(fb);

free_crtc:
	drmModeFreeCrtc(crtc);

exit:
	if (src)
		munmap(src, src_size);

	return ret;
}

/** Let the XFree86 code know the Setup() function. */
static MODULESETUPPROTO(ARMSOCSetup);

/** Provide basic version information to the XFree86 code. */
static XF86ModuleVersionInfo ARMSOCVersRec = {
		ARMSOC_DRIVER_NAME,
		MODULEVENDORSTRING,
		MODINFOSTRING1,
		MODINFOSTRING2,
		XORG_VERSION_CURRENT,
		PACKAGE_VERSION_MAJOR,
		PACKAGE_VERSION_MINOR,
		PACKAGE_VERSION_PATCHLEVEL,
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

		/* The return value must be non-NULL on success even
		 * though there is no TearDownProc.
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
 * an identifying message.
 */
static void
ARMSOCIdentify(int flags)
{
	xf86Msg(X_INFO, "%s: Driver for ARM Mali compatible chipsets\n", ARMSOC_NAME);
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
	GDevPtr *devSections = NULL;
	int numDevSections;
	Bool foundScreen = FALSE;

	/* Get the "xorg.conf" file device sections that match this driver, and
	 * return (error out) if there are none:
	 */
	numDevSections = xf86MatchDevice(ARMSOC_DRIVER_NAME, &devSections);
	if (numDevSections <= 0) {
		EARLY_ERROR_MSG(
				"Did not find any matching device section in configuration file");
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
		int fd;

		if (devSections) {
			const char *busIdStr;
			const char *driverNameStr;
			const char *cardNumStr;

			/* get the Bus ID */
			busIdStr = xf86FindOptionValue(
						devSections[i]->options,
						"BusID");

			/* get the DriverName */
			driverNameStr = xf86FindOptionValue(
					devSections[i]->options,
					"DriverName");

			/* get the card_num from xorg.conf if present */
			cardNumStr = xf86FindOptionValue(
						devSections[i]->options,
						"DRICard");

			if (busIdStr && driverNameStr) {
					EARLY_WARNING_MSG(
						"Option DriverName ignored (BusID is specified)");
			}
			if (busIdStr || driverNameStr) {
				if (cardNumStr) {
					EARLY_WARNING_MSG(
						"Option DRICard ignored (BusID or DriverName are specified)");
				}
			}

			if (busIdStr) {
				if (0 == strlen(busIdStr)) {
					EARLY_ERROR_MSG(
						"Missing value for Option BusID");
					return FALSE;
				}
				connection.bus_id = busIdStr;
			} else if (driverNameStr) {
				if (0 == strlen(driverNameStr)) {
					EARLY_ERROR_MSG(
						"Missing value for Option DriverName");
					return FALSE;
				}
				connection.driver_name = driverNameStr;
			} else if (cardNumStr) {
				char *endptr;
				errno = 0;
				connection.card_num = strtol(cardNumStr,
						&endptr, 10);
				if (('\0' == *cardNumStr) ||
					('\0' != *endptr) ||
					(errno != 0)) {
					EARLY_ERROR_MSG(
							"Bad Option DRICard value : %s",
							cardNumStr);
					return FALSE;
				}
			}
		}
		fd = ARMSOCOpenDRMCard();
		if (fd >= 0) {
			struct ARMSOCRec *pARMSOC;

			/* Allocate the ScrnInfoRec */
			pScrn = xf86AllocateScreen(drv, 0);
			if (!pScrn) {
				EARLY_ERROR_MSG(
						"Cannot allocate a ScrnInfoPtr");
				return FALSE;
			}
			/* Allocate the driver's Screen-specific, "private"
			 * data structure and hook it into the ScrnInfoRec's
			 * driverPrivate field.
			 */
			pScrn->driverPrivate =
					calloc(1, sizeof(struct ARMSOCRec));
			if (!pScrn->driverPrivate)
				return FALSE;

			pARMSOC = ARMSOCPTR(pScrn);
			/* initially mark to use all DRM crtcs */
			pARMSOC->crtcNum = -1;

			if (flags & PROBE_DETECT) {
				/* just add the device.. we aren't a PCI device,
				 * so call xf86AddBusDeviceToConfigure()
				 * directly
				 */
				xf86AddBusDeviceToConfigure(ARMSOC_DRIVER_NAME,
						BUS_NONE, NULL, i);
				foundScreen = TRUE;
				drmClose(fd);
				continue;
			}

			if (devSections) {
				int entity = xf86ClaimNoSlot(drv, 0,
						devSections[i], TRUE);
				xf86AddEntityToScreen(pScrn, entity);
			}

			/* if there are multiple screens, use a separate
			 * crtc for each one
			 */
			if (numDevSections > 1)
				pARMSOC->crtcNum = i;

			xf86Msg(X_INFO, "Screen:%d,  CRTC:%d\n",
					pScrn->scrnIndex,
					pARMSOC->crtcNum);

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

/* Find a drmmode driver with the same name as the underlying
 * drm kernel driver */
static struct drmmode_interface *get_drmmode_implementation(int drm_fd)
{
	drmVersionPtr version;
	struct drmmode_interface *ret = NULL;
	struct drmmode_interface *ifaces[] = {
		&exynos_interface,
		&pl111_interface,
		&meson_interface,
	};
	int i;

	version = drmGetVersion(drm_fd);
	if (!version)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(ifaces); i++) {
		struct drmmode_interface *iface = ifaces[i];
		if (strcmp(version->name, iface->driver_name) == 0) {
			ret = iface;
			break;
		}
	}

	drmFreeVersion(version);
	return ret;
}

/**
 * The driver's PreInit() function.  Additional hardware probing is allowed
 * now, including display configuration.
 */
static Bool
ARMSOCPreInit(ScrnInfoPtr pScrn, int flags)
{
	struct ARMSOCRec *pARMSOC;
	int default_depth, fbbpp;
	rgb defaultWeight = { 0, 0, 0 };
	rgb defaultMask = { 0, 0, 0 };
	Gamma defaultGamma = { 0.0, 0.0, 0.0 };
	int driNumBufs;

	TRACE_ENTER();

	if (flags & PROBE_DETECT) {
		ERROR_MSG(
				"The %s driver does not support the \"-configure\" or \"-probe\" command line arguments.",
				ARMSOC_NAME);
		return FALSE;
	}

	/* Check the number of entities, and fail if it isn't one. */
	if (pScrn->numEntities != 1) {
		ERROR_MSG(
				"Driver expected 1 entity, but found %d for screen %d",
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
		ERROR_MSG(
				"The requested default visual (%s) has an unsupported depth (%d).",
				xf86GetVisualName(pScrn->defaultVisual),
					pScrn->depth);
		goto fail;
	}

	/* Using a programmable clock: */
	pScrn->progClock = TRUE;

	/* Open a connection to the DRM, so we can communicate
	 * with the KMS code:
	 */
	if (!ARMSOCOpenDRM(pScrn))
		goto fail;

	/* Optional umplock support. */
	pARMSOC->umplock_fd = open("/dev/umplock", O_RDWR);
	if (pARMSOC->umplock_fd < 0)
		WARNING_MSG("Failed to open /dev/umplock.");

	pARMSOC->drmmode_interface =
			get_drmmode_implementation(pARMSOC->drmFD);
	if (!pARMSOC->drmmode_interface)
		goto fail2;

	/* create DRM device instance: */
	pARMSOC->dev = armsoc_device_new(pARMSOC->drmFD,
			pARMSOC->drmmode_interface->create_custom_gem);

	/* set chipset name: */
	pScrn->chipset = (char *)ARMSOC_CHIPSET_NAME;
	INFO_MSG("Chipset: %s", pScrn->chipset);

	/*
	 * Process the "xorg.conf" file options:
	 */
	xf86CollectOptions(pScrn, NULL);
	pARMSOC->pOptionInfo = calloc(1, sizeof(ARMSOCOptions));
	if (!pARMSOC->pOptionInfo)
		goto fail2;

	memcpy(pARMSOC->pOptionInfo, ARMSOCOptions, sizeof(ARMSOCOptions));
	xf86ProcessOptions(pScrn->scrnIndex,
			pARMSOC->pEntityInfo->device->options,
			pARMSOC->pOptionInfo);

	/* Determine if the user wants debug messages turned on: */
	armsocDebug = xf86ReturnOptValBool(pARMSOC->pOptionInfo,
					OPTION_DEBUG, FALSE);

	if (!xf86GetOptValInteger(pARMSOC->pOptionInfo, OPTION_DRI_NUM_BUF,
			&driNumBufs)) {
		/* Default to double buffering */
		driNumBufs = 2;
	}

	if (driNumBufs < 2) {
		ERROR_MSG(
			"Invalid option for %s: %d. Must be greater than or equal to 2",
			xf86TokenToOptName(pARMSOC->pOptionInfo,
				OPTION_DRI_NUM_BUF),
			driNumBufs);
		return FALSE;
	}
	pARMSOC->driNumBufs = driNumBufs;
	/* Determine if user wants to disable buffer flipping: */
	pARMSOC->NoFlip = xf86ReturnOptValBool(pARMSOC->pOptionInfo,
			OPTION_NO_FLIP, FALSE);
	INFO_MSG("Buffer Flipping is %s",
				pARMSOC->NoFlip ? "Disabled" : "Enabled");

	/*
	 * Select the video modes:
	 */
	INFO_MSG("Setting the video modes ...");

	/* Don't call drmCheckModesettingSupported() as its written only for
	 * PCI devices.
	 */

	/* Do initial KMS setup: */
	if (!drmmode_pre_init(pScrn, pARMSOC->drmFD,
			(pScrn->bitsPerPixel >> 3))) {
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
		ERROR_MSG(
				"The requested number of bits per pixel (%d) is unsupported.",
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
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	if (!pARMSOC->pARMSOCEXA)
		pARMSOC->pARMSOCEXA = InitNullEXA(pScreen, pScrn,
								pARMSOC->drmFD);

	if (pARMSOC->pARMSOCEXA)
		pARMSOC->dri = ARMSOCDRI2ScreenInit(pScreen);
	else
		pARMSOC->dri = FALSE;
}

/**
 * The driver's ScreenInit() function, called at the start of each server
 * generation. Fill in pScreen, map the frame buffer, save state,
 * initialize the mode, etc.
 */
static Bool
ARMSOCScreenInit(SCREEN_INIT_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
#ifndef XF86_SCRN_INTERFACE
	int scrnIndex = pScrn->scrnIndex;
#endif
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	VisualPtr visual;
	xf86CrtcConfigPtr xf86_config;
	int j;
	int width, height;

	TRACE_ENTER();

	pARMSOC->created_scanout_pixmap = FALSE;

	/* set drm master before allocating scanout buffer */
	if (ARMSOCSetDRMMaster()) {
		ERROR_MSG("Cannot get DRM master: %s", strerror(errno));
		goto fail;
	}
	/* Allocate initial scanout buffer */
	DEBUG_MSG("allocating new scanout buffer: %dx%d",
			pScrn->virtualX, pScrn->virtualY);
	assert(!pARMSOC->scanout);
	width = pScrn->currentMode->HDisplay
	      + 2*(pScrn->currentMode->HSkew >> 8);
	height = pScrn->currentMode->VDisplay
	       + 2*(pScrn->currentMode->HSkew & 0xFF);
	if (pScrn->virtualX > width)
		width = pScrn->virtualX;
	if (pScrn->virtualY > height)
		height = pScrn->virtualY;
	pARMSOC->scanout = armsoc_bo_new_with_dim(pARMSOC->dev, width,
			height, pScrn->bitsPerPixel, pScrn->bitsPerPixel,
			ARMSOC_BO_SCANOUT);
	if (!pARMSOC->scanout) {
		ERROR_MSG("Cannot allocate scanout buffer\n");
		goto fail1;
	}
	pScrn->displayWidth = armsoc_bo_pitch(pARMSOC->scanout) /
			((pScrn->bitsPerPixel+7) / 8);
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
	if (!miSetVisualTypes(pScrn->bitsPerPixel,
			miGetDefaultVisualMask(pScrn->depth),
			pScrn->rgbBits, pScrn->defaultVisual)) {
		ERROR_MSG(
				"Cannot initialize the visual type for %d bits per pixel!",
				pScrn->bitsPerPixel);
		goto fail2;
	}

	if (pScrn->bitsPerPixel == 32 && pScrn->depth == 24) {
		/* Also add a 24 bit depth visual */
		if (!miSetVisualTypes(24, miGetDefaultVisualMask(pScrn->depth),
				pScrn->rgbBits, pScrn->defaultVisual)) {
			WARNING_MSG(
					"Cannot initialize a 24 depth visual for 32bpp");
		} else {
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

	/* Continue initializing the generic 2D drawing functions after
	 * fixing the RGB ordering:
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

	/* Enable cursor position updates by mouse signal handler: */
	xf86SetSilkenMouse(pScreen);

	/* Initialize the cursor: */
	if (!miDCInitialize(pScreen, xf86GetPointerScreenFuncs())) {
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

	/* Take over the virtual terminal from the console, set the
	 * desired mode, etc.:
	 */
	if (0) {
		if (!ARMSOCEnterVT(VT_FUNC_ARGS(0))) {
			ERROR_MSG("ARMSOCEnterVT() failed!");
			goto fail6;
		}
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
	wrap(pARMSOC, pScreen, CreateScreenResources,
			ARMSOCCreateScreenResources);
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
	if (pARMSOC->dri)
		ARMSOCDRI2CloseScreen(pScreen);

	if (pARMSOC->pARMSOCEXA)
		if (pARMSOC->pARMSOCEXA->CloseScreen)
			pARMSOC->pARMSOCEXA->CloseScreen(CLOSE_SCREEN_ARGS);
fail4:
	/* Call the CloseScreen functions for fbInitScreen,  miDCInitialize,
	 * exaDriverInit & xf86CrtcScreenInit as appropriate via their
	 * wrapped pointers.
	 * exaDDXCloseScreen uses the XF86SCRNINFO macro so we must
	 * set up the key for this before it gets called.
	 */
	dixSetPrivate(&pScreen->devPrivates, xf86ScreenKey, pScrn);
	(*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);

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
		LOCO *colors, VisualPtr pVisual)
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
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	Bool ret;

	TRACE_ENTER();

	drmmode_screen_fini(pScrn);
	drmmode_cursor_fini(pScreen);

	/* pScreen->devPrivate holds the root pixmap created around our bo by miCreateResources which is installed
	 * by fbScreenInit() when called from ARMSOCScreenInit().
	 * This pixmap should be destroyed in miScreenClose() but this isn't wrapped by fbScreenInit() so to prevent a leak
	 * we do it here, before calling the CloseScreen chain which would just free pScreen->devPrivate in fbCloseScreen()
	 */
	if (pScreen->devPrivate) {
		(void) (*pScreen->DestroyPixmap)(pScreen->devPrivate);
		pScreen->devPrivate = NULL;
	}

	unwrap(pARMSOC, pScreen, CloseScreen);
	unwrap(pARMSOC, pScreen, BlockHandler);
	unwrap(pARMSOC, pScreen, CreateScreenResources);

	ret = (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);

	if (pARMSOC->dri)
		ARMSOCDRI2CloseScreen(pScreen);

	if (pARMSOC->pARMSOCEXA)
		if (pARMSOC->pARMSOCEXA->CloseScreen)
			pARMSOC->pARMSOCEXA->CloseScreen(CLOSE_SCREEN_ARGS);

	armsoc_bo_unreference(pARMSOC->scanout);
	pARMSOC->scanout = NULL;

	pScrn->displayWidth = 0;

	if (pScrn->vtSema == TRUE)
		ARMSOCLeaveVT(VT_FUNC_ARGS(0));

	pScrn->vtSema = FALSE;

	TRACE_EXIT();

	return ret;
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
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	swap(pARMSOC, pScreen, CreateScreenResources);
	if (!(*pScreen->CreateScreenResources) (pScreen))
		return FALSE;
	swap(pARMSOC, pScreen, CreateScreenResources);

	if (ARMSOCCopyDRMFB(pScrn)) {
		/* Only allow None BG root if we initialized the scanout
		 * buffer */
		pScreen->canDoBGNoneRoot = TRUE;
	}

	if (!xf86SetDesiredModes(pScrn)) {
		ERROR_MSG("xf86SetDesiredModes() failed!");
		return FALSE;
	}

	return TRUE;
}


static void
ARMSOCBlockHandler(BLOCKHANDLER_ARGS_DECL)
{
	SCREEN_PTR(arg);
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

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
	if (ret)
		WARNING_MSG("drmDropMaster failed: %s", strerror(errno));

	TRACE_EXIT();
}

/**
 * The driver's FreeScreen() function.
 * Frees the ScrnInfoRec driverPrivate field when a screen is
 * deleted by the common layer.
 * This function is not used in normal (error free) operation.
 */
static void
ARMSOCFreeScreen(FREE_SCREEN_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	TRACE_ENTER();

	if (!pARMSOC) {
		/* This can happen if a Screen is deleted after Probe(): */
		return;
	}

	if (pARMSOC->pARMSOCEXA) {
		if (pARMSOC->pARMSOCEXA->FreeScreen)
			pARMSOC->pARMSOCEXA->FreeScreen(
					FREE_SCREEN_ARGS(pScrn));
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

