/*
 * Copyright © 2007 Red Hat, Inc.
 * Copyright © 2008 Maarten Maathuis
 * Copyright © 2011 Texas Instruments, Inc
 *
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
 *    Dave Airlie <airlied@redhat.com>
 *    Ian Elliott <ianelliottus@yahoo.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86DDC.h"
#include "xf86RandR12.h"

#ifdef HAVE_XEXTPROTO_71
#include <X11/extensions/dpmsconst.h>
#else
#define DPMS_SERVER
#include <X11/extensions/dpms.h>
#endif

#include "armsoc_driver.h"

#include "xf86drmMode.h"
#include "drm_fourcc.h"
#include "X11/Xatom.h"

#include <libudev.h>
#include "drmmode_driver.h"

struct drmmode_cursor_rec {
	/* hardware cursor: */
	struct armsoc_bo *bo;
	int x, y;
	 /* These are used for HWCURSOR_API_PLANE */
	drmModePlane *ovr;
	uint32_t fb_id;
	/* This is used for HWCURSOR_API_STANDARD */
	uint32_t handle;
};

struct drmmode_rec {
	int fd;
	drmModeResPtr mode_res;
	int cpp;
	struct udev_monitor *uevent_monitor;
	InputHandlerProc uevent_handler;
	struct drmmode_cursor_rec *cursor;
};

struct drmmode_crtc_private_rec {
	struct drmmode_rec *drmmode;
	uint32_t crtc_id;
	int cursor_visible;
	/* settings retained on last good modeset */
	int last_good_x;
	int last_good_y;
	Rotation last_good_rotation;
	DisplayModePtr last_good_mode;
};

struct drmmode_prop_rec {
	drmModePropertyPtr mode_prop;
	/* Index within the kernel-side property arrays for this connector. */
	int index;
	/* if range prop, num_atoms == 1;
	 * if enum prop, num_atoms == num_enums + 1
	 */
	int num_atoms;
	Atom *atoms;
};

struct drmmode_output_priv {
	struct drmmode_rec *drmmode;
	int output_id;
	drmModeConnectorPtr connector;
	drmModeEncoderPtr *encoders;
	drmModePropertyBlobPtr edid_blob;
	int num_props;
	struct drmmode_prop_rec *props;
	int enc_mask;   /* encoders present (mask of encoder indices) */
	int enc_clones; /* encoder clones possible (mask of encoder indices) */
};

static void drmmode_output_dpms(xf86OutputPtr output, int mode);
static Bool resize_scanout_bo(ScrnInfoPtr pScrn, int width, int height);

static struct drmmode_rec *
drmmode_from_scrn(ScrnInfoPtr pScrn)
{
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
	struct drmmode_crtc_private_rec *drmmode_crtc;

	drmmode_crtc = xf86_config->crtc[0]->driver_private;
	return drmmode_crtc->drmmode;
}

static void
drmmode_ConvertFromKMode(ScrnInfoPtr pScrn, drmModeModeInfo *kmode,
		DisplayModePtr	mode)
{
	memset(mode, 0, sizeof(DisplayModeRec));
	mode->status = MODE_OK;

	mode->Clock = kmode->clock;

	mode->HDisplay = kmode->hdisplay;
	mode->HSyncStart = kmode->hsync_start;
	mode->HSyncEnd = kmode->hsync_end;
	mode->HTotal = kmode->htotal;
	mode->HSkew = kmode->hskew;

	mode->VDisplay = kmode->vdisplay;
	mode->VSyncStart = kmode->vsync_start;
	mode->VSyncEnd = kmode->vsync_end;
	mode->VTotal = kmode->vtotal;
	mode->VScan = kmode->vscan;

	mode->Flags = kmode->flags;
	mode->name = strdup(kmode->name);

	DEBUG_MSG("copy mode %s (%p %p)", kmode->name, mode->name, mode);

	if (kmode->type & DRM_MODE_TYPE_DRIVER)
		mode->type = M_T_DRIVER;

	if (kmode->type & DRM_MODE_TYPE_PREFERRED)
		mode->type |= M_T_PREFERRED;

	xf86SetModeCrtc(mode, pScrn->adjustFlags);
}

static void
drmmode_ConvertToKMode(ScrnInfoPtr pScrn, drmModeModeInfo *kmode,
		DisplayModePtr mode)
{
	memset(kmode, 0, sizeof(*kmode));

	kmode->clock = mode->Clock;
	kmode->hdisplay = mode->HDisplay;
	kmode->hsync_start = mode->HSyncStart;
	kmode->hsync_end = mode->HSyncEnd;
	kmode->htotal = mode->HTotal;
	kmode->hskew = mode->HSkew;

	kmode->vdisplay = mode->VDisplay;
	kmode->vsync_start = mode->VSyncStart;
	kmode->vsync_end = mode->VSyncEnd;
	kmode->vtotal = mode->VTotal;
	kmode->vscan = mode->VScan;

	kmode->flags = mode->Flags;
	if (mode->name)
		strncpy(kmode->name, mode->name, DRM_DISPLAY_MODE_LEN);
	kmode->name[DRM_DISPLAY_MODE_LEN-1] = 0;
}

static void
drmmode_crtc_dpms(xf86CrtcPtr drmmode_crtc, int mode)
{
	/* TODO: MIDEGL-1431: Implement this function */
}

static int
drmmode_revert_mode(xf86CrtcPtr crtc, uint32_t *output_ids, int output_count)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;
	uint32_t fb_id;
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	drmModeModeInfo kmode;

	if (!drmmode_crtc->last_good_mode) {
		DEBUG_MSG("No last good values to use");
		return FALSE;
	}

	/* revert to last good settings */
	DEBUG_MSG("Reverting to last_good values");
	if (!resize_scanout_bo(pScrn,
			drmmode_crtc->last_good_mode->HDisplay,
			drmmode_crtc->last_good_mode->VDisplay)) {
		ERROR_MSG("Could not revert to last good mode");
		return FALSE;
	}

	fb_id = armsoc_bo_get_fb(pARMSOC->scanout);
	drmmode_ConvertToKMode(crtc->scrn, &kmode,
			drmmode_crtc->last_good_mode);
	drmModeSetCrtc(drmmode_crtc->drmmode->fd,
			drmmode_crtc->crtc_id,
			fb_id,
			drmmode_crtc->last_good_x,
			drmmode_crtc->last_good_y,
			output_ids, output_count, &kmode);

	/* let RandR know we changed things */
	xf86RandR12TellChanged(pScrn->pScreen);

	return TRUE;
}

static Bool
drmmode_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode,
		Rotation rotation, int x, int y)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
	struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;
	struct drmmode_rec *drmmode = drmmode_crtc->drmmode;
	uint32_t *output_ids = NULL;
	int output_count = 0;
	int ret = TRUE;
	int err;
	int i;
	uint32_t fb_id;
	drmModeModeInfo kmode;
	drmModeCrtcPtr newcrtc = NULL;

	TRACE_ENTER();

	fb_id = armsoc_bo_get_fb(pARMSOC->scanout);

	if (fb_id == 0) {
		DEBUG_MSG("create framebuffer: %dx%d",
				pScrn->virtualX, pScrn->virtualY);

		err = armsoc_bo_add_fb(pARMSOC->scanout);
		if (err) {
			ERROR_MSG(
					"Failed to add framebuffer to the scanout buffer");
			return FALSE;
		}

		fb_id = armsoc_bo_get_fb(pARMSOC->scanout);
		if (0 == fb_id)
			return FALSE;
	}

	/* Set the new mode: */
	crtc->mode = *mode;
	crtc->x = x;
	crtc->y = y;
	crtc->rotation = rotation;

	output_ids = calloc(xf86_config->num_output, sizeof *output_ids);
	if (!output_ids) {
		ERROR_MSG(
				"memory allocation failed in drmmode_set_mode_major()");
		ret = FALSE;
		goto cleanup;
	}

	for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];
		struct drmmode_output_priv *drmmode_output;

		if (output->crtc != crtc)
			continue;

		drmmode_output = output->driver_private;
		output_ids[output_count] =
				drmmode_output->connector->connector_id;
		output_count++;
	}

	if (!xf86CrtcRotate(crtc)) {
		ERROR_MSG(
				"failed to assign rotation in drmmode_set_mode_major()");
		ret = FALSE;
		goto cleanup;
	}

	if (crtc->funcs->gamma_set)
		crtc->funcs->gamma_set(crtc, crtc->gamma_red, crtc->gamma_green,
				       crtc->gamma_blue, crtc->gamma_size);

	drmmode_ConvertToKMode(crtc->scrn, &kmode, mode);

	err = drmModeSetCrtc(drmmode->fd, drmmode_crtc->crtc_id,
			fb_id, x, y, output_ids, output_count, &kmode);
	if (err) {
		ERROR_MSG(
				"drm failed to set mode: %s", strerror(-err));

		ret = FALSE;
		if (!drmmode_revert_mode(crtc, output_ids, output_count))
			goto cleanup;
		else
			goto done_setting;
	}

	/* get the actual crtc info */
	newcrtc = drmModeGetCrtc(drmmode->fd, drmmode_crtc->crtc_id);
	if (!newcrtc) {
		ERROR_MSG("couldn't get actual mode back");

		ret = FALSE;
		if (!drmmode_revert_mode(crtc, output_ids, output_count))
			goto cleanup;
		else
			goto done_setting;
	}

	if (kmode.hdisplay != newcrtc->mode.hdisplay ||
		kmode.vdisplay != newcrtc->mode.vdisplay) {

		ERROR_MSG(
			"drm did not set requested mode! (requested %dx%d, actual %dx%d)",
			kmode.hdisplay, kmode.vdisplay,
			newcrtc->mode.hdisplay,
			newcrtc->mode.vdisplay);

		ret = FALSE;
		if (!drmmode_revert_mode(crtc, output_ids, output_count))
			goto cleanup;
		else
			goto done_setting;
	}

	/* When called on a resize, crtc->mode already contains the
	 * resized values so we can't use this for recovery.
	 * We can't read it out of the crtc either as mode_valid is 0.
	 * Instead we save the last good mode set here & fallback to
	 * that on failure.
	 */
	DEBUG_MSG("Saving last good values");
	drmmode_crtc->last_good_x = crtc->x;
	drmmode_crtc->last_good_y = crtc->y;
	drmmode_crtc->last_good_rotation = crtc->rotation;
	if (drmmode_crtc->last_good_mode) {
		if (drmmode_crtc->last_good_mode->name)
			free(drmmode_crtc->last_good_mode->name);
		free(drmmode_crtc->last_good_mode);
	}
	drmmode_crtc->last_good_mode = xf86DuplicateMode(&crtc->mode);

	ret = TRUE;

done_setting:
	/* Turn on any outputs on this crtc that may have been disabled: */
	for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];

		if (output->crtc != crtc)
			continue;

		drmmode_output_dpms(output, DPMSModeOn);
	}

	/* if hw cursor is initialized, reload it */
	if (drmmode->cursor)
		xf86_reload_cursors(pScrn->pScreen);

cleanup:
	if (newcrtc)
		drmModeFreeCrtc(newcrtc);

	if (output_ids)
		free(output_ids);

	if (!ret && !drmmode_crtc->last_good_mode) {
		/* If there was a problem, restore the last good mode: */
		crtc->x = drmmode_crtc->last_good_x;
		crtc->y = drmmode_crtc->last_good_y;
		crtc->rotation = drmmode_crtc->last_good_rotation;
		crtc->mode = *drmmode_crtc->last_good_mode;
	}

	TRACE_EXIT();
	return ret;
}

static void
drmmode_hide_cursor(xf86CrtcPtr crtc)
{
	struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;
	struct drmmode_rec *drmmode = drmmode_crtc->drmmode;
	struct drmmode_cursor_rec *cursor = drmmode->cursor;
	ScrnInfoPtr pScrn = crtc->scrn;
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	if (!cursor)
		return;

	drmmode_crtc->cursor_visible = FALSE;

	if (pARMSOC->drmmode_interface->cursor_api == HWCURSOR_API_PLANE) {
		/* set plane's fb_id to 0 to disable it */
		drmModeSetPlane(drmmode->fd, cursor->ovr->plane_id,
				drmmode_crtc->crtc_id, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0);
	} else { /* HWCURSOR_API_STANDARD */
		/* set handle to 0 to disable the cursor */
		drmModeSetCursor(drmmode->fd, drmmode_crtc->crtc_id,
				 0, 0, 0);
	}
}

/*
 * The argument "update_image" controls whether the cursor image needs
 * to be updated by the HW or not. This is ignored by HWCURSOR_API_PLANE
 * which doesn't allow changing the cursor position without updating
 * the image too.
 */
static void
drmmode_show_cursor_image(xf86CrtcPtr crtc, Bool update_image)
{
	struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;
	struct drmmode_rec *drmmode = drmmode_crtc->drmmode;
	struct drmmode_cursor_rec *cursor = drmmode->cursor;
	int crtc_x, crtc_y, src_x, src_y;
	int w, h, pad;
	ScrnInfoPtr pScrn = crtc->scrn;
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	if (!cursor)
		return;

	drmmode_crtc->cursor_visible = TRUE;

	w = pARMSOC->drmmode_interface->cursor_width;
	h = pARMSOC->drmmode_interface->cursor_height;
	pad = pARMSOC->drmmode_interface->cursor_padding;

	/* get padded width */
	w = w + 2 * pad;
	/* get x of padded cursor */
	crtc_x = cursor->x - pad;
	crtc_y = cursor->y;

	if (pARMSOC->drmmode_interface->cursor_api == HWCURSOR_API_PLANE) {
		src_x = 0;
		src_y = 0;

		/* calculate clipped x, y, w & h if cursor is off edges */
		if (crtc_x < 0) {
			src_x += -crtc_x;
			w -= -crtc_x;
			crtc_x = 0;
		}

		if (crtc_y < 0) {
			src_y += -crtc_y;
			h -= -crtc_y;
			crtc_y = 0;
		}

		if ((crtc_x + w) > crtc->mode.HDisplay)
			w = crtc->mode.HDisplay - crtc_x;

		if ((crtc_y + h) > crtc->mode.VDisplay)
			h = crtc->mode.VDisplay - crtc_y;

		/* note src coords (last 4 args) are in Q16 format */
		drmModeSetPlane(drmmode->fd, cursor->ovr->plane_id,
			drmmode_crtc->crtc_id, cursor->fb_id, 0,
			crtc_x, crtc_y, w, h, src_x<<16, src_y<<16,
			w<<16, h<<16);
	} else {
		if (update_image)
			drmModeSetCursor(drmmode->fd,
					 drmmode_crtc->crtc_id,
					 cursor->handle, w, h);
		drmModeMoveCursor(drmmode->fd,
				  drmmode_crtc->crtc_id,
				  crtc_x, crtc_y);
	}
}

static void
drmmode_show_cursor(xf86CrtcPtr crtc)
{
	drmmode_show_cursor_image(crtc, TRUE);
}

static void
drmmode_set_cursor_position(xf86CrtcPtr crtc, int x, int y)
{
	struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;
	struct drmmode_rec *drmmode = drmmode_crtc->drmmode;
	struct drmmode_cursor_rec *cursor = drmmode->cursor;

	if (!cursor)
		return;

	cursor->x = x;
	cursor->y = y;

	/*
	 * Show the cursor at a different position without updating the image
	 * when possible (HWCURSOR_API_PLANE doesn't have a way to update
	 * cursor position without updating the image too).
	 */
	drmmode_show_cursor_image(crtc, FALSE);
}

/*
 * The cursor format is ARGB so the image can be copied straight over.
 * Columns of CURSORPAD blank pixels are maintained down either side
 * of the destination image. This is a workaround for a bug causing
 * corruption when the cursor reaches the screen edges in some DRM
 * drivers.
 */
static void set_cursor_image(xf86CrtcPtr crtc, uint32_t *d, CARD32 *s)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	int row;
	void *dst;
	const char *src_row;
	char *dst_row;
	uint32_t cursorh = pARMSOC->drmmode_interface->cursor_height;
	uint32_t cursorw = pARMSOC->drmmode_interface->cursor_width;
	uint32_t cursorpad = pARMSOC->drmmode_interface->cursor_padding;

	dst = d;
	for (row = 0; row < cursorh; row += 1) {
		/* we're operating with ARGB data (4 bytes per pixel) */
		src_row = (const char *)s + row * 4 * cursorw;
		dst_row = (char *)dst + row * 4 * (cursorw + 2 * cursorpad);

		/* set first CURSORPAD pixels in row to 0 */
		memset(dst_row, 0, (4 * cursorpad));
		/* copy cursor image pixel row across */
		memcpy(dst_row + (4 * cursorpad), src_row, 4 * cursorw);
		/* set last CURSORPAD pixels in row to 0 */
		memset(dst_row + 4 * (cursorpad + cursorw),
				0, (4 * cursorpad));
	}
}

static void
drmmode_load_cursor_argb(xf86CrtcPtr crtc, CARD32 *image)
{
	uint32_t *d;
	struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;
	struct drmmode_rec *drmmode = drmmode_crtc->drmmode;
	struct drmmode_cursor_rec *cursor = drmmode->cursor;
	int visible;

	if (!cursor)
		return;

	visible = drmmode_crtc->cursor_visible;

	if (visible)
		drmmode_hide_cursor(crtc);

	d = armsoc_bo_map(cursor->bo);
	if (!d) {
		xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
			"load_cursor_argb map failure\n");
		if (visible)
			drmmode_show_cursor_image(crtc, TRUE);
		return;
	}

	set_cursor_image(crtc, d, image);

	if (visible)
		drmmode_show_cursor_image(crtc, TRUE);
}

static Bool
drmmode_cursor_init_plane(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct drmmode_rec *drmmode = drmmode_from_scrn(pScrn);
	struct drmmode_cursor_rec *cursor;
	drmModePlaneRes *plane_resources;
	drmModePlane *ovr;
	int w, h, pad;
	uint32_t handles[4], pitches[4], offsets[4]; /* we only use [0] */

	if (drmmode->cursor) {
		INFO_MSG("cursor already initialized");
		return TRUE;
	}

	if (!xf86LoaderCheckSymbol("drmModeGetPlaneResources")) {
		ERROR_MSG(
				"HW cursor not supported (needs libdrm 2.4.30 or higher)");
		return FALSE;
	}

	/* find an unused plane which can be used as a mouse cursor.  Note
	 * that we cheat a bit, in order to not burn one overlay per crtc,
	 * and only show the mouse cursor on one crtc at a time
	 */
	plane_resources = drmModeGetPlaneResources(drmmode->fd);
	if (!plane_resources) {
		ERROR_MSG("HW cursor: drmModeGetPlaneResources failed: %s",
						strerror(errno));
		return FALSE;
	}

	if (plane_resources->count_planes < 1) {
		ERROR_MSG("not enough planes for HW cursor");
		drmModeFreePlaneResources(plane_resources);
		return FALSE;
	}

	ovr = drmModeGetPlane(drmmode->fd, plane_resources->planes[0]);
	if (!ovr) {
		ERROR_MSG("HW cursor: drmModeGetPlane failed: %s",
					strerror(errno));
		drmModeFreePlaneResources(plane_resources);
		return FALSE;
	}

	if (pARMSOC->drmmode_interface->init_plane_for_cursor &&
		pARMSOC->drmmode_interface->init_plane_for_cursor(
				drmmode->fd, ovr->plane_id)) {
		ERROR_MSG("Failed driver-specific cursor initialization");
		drmModeFreePlaneResources(plane_resources);
		return FALSE;
	}

	cursor = calloc(1, sizeof(struct drmmode_cursor_rec));
	if (!cursor) {
		ERROR_MSG("HW cursor: calloc failed");
		drmModeFreePlane(ovr);
		drmModeFreePlaneResources(plane_resources);
		return FALSE;
	}

	cursor->ovr = ovr;

	w = pARMSOC->drmmode_interface->cursor_width;
	h = pARMSOC->drmmode_interface->cursor_height;
	pad = pARMSOC->drmmode_interface->cursor_padding;

	/* allow for cursor padding in the bo */
	cursor->bo  = armsoc_bo_new_with_dim(pARMSOC->dev,
				w + 2 * pad, h,
				0, 32, ARMSOC_BO_SCANOUT);

	if (!cursor->bo) {
		ERROR_MSG("HW cursor: buffer allocation failed");
		free(cursor);
		drmModeFreePlane(ovr);
		drmModeFreePlaneResources(plane_resources);
		return FALSE;
	}

	handles[0] = armsoc_bo_handle(cursor->bo);
	pitches[0] = armsoc_bo_pitch(cursor->bo);
	offsets[0] = 0;

	/* allow for cursor padding in the fb */
	if (drmModeAddFB2(drmmode->fd, w + 2 * pad, h, DRM_FORMAT_ARGB8888,
			handles, pitches, offsets, &cursor->fb_id, 0)) {
		ERROR_MSG("HW cursor: drmModeAddFB2 failed: %s",
					strerror(errno));
		armsoc_bo_unreference(cursor->bo);
		free(cursor);
		drmModeFreePlane(ovr);
		drmModeFreePlaneResources(plane_resources);
		return FALSE;
	}

	if (!xf86_cursors_init(pScreen, w, h, HARDWARE_CURSOR_ARGB)) {
		ERROR_MSG("xf86_cursors_init() failed");
		if (drmModeRmFB(drmmode->fd, cursor->fb_id))
			ERROR_MSG("drmModeRmFB() failed");

		armsoc_bo_unreference(cursor->bo);
		free(cursor);
		drmModeFreePlane(ovr);
		drmModeFreePlaneResources(plane_resources);
		return FALSE;
	}

	INFO_MSG("HW cursor initialized");
	drmmode->cursor = cursor;
	drmModeFreePlaneResources(plane_resources);
	return TRUE;
}

static Bool
drmmode_cursor_init_standard(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct drmmode_rec *drmmode = drmmode_from_scrn(pScrn);
	struct drmmode_cursor_rec *cursor;
	int w, h, pad;

	if (drmmode->cursor) {
		INFO_MSG("cursor already initialized");
		return TRUE;
	}

	if (!xf86LoaderCheckSymbol("drmModeSetCursor") ||
	    !xf86LoaderCheckSymbol("drmModeMoveCursor")) {
		ERROR_MSG(
				"Standard HW cursor not supported (needs libdrm 2.4.3 or higher)");
		return FALSE;
	}

	cursor = calloc(1, sizeof(struct drmmode_cursor_rec));
	if (!cursor) {
		ERROR_MSG("HW cursor (standard): calloc failed");
		return FALSE;
	}

	w = pARMSOC->drmmode_interface->cursor_width;
	h = pARMSOC->drmmode_interface->cursor_height;
	pad = pARMSOC->drmmode_interface->cursor_padding;

	/* allow for cursor padding in the bo */
	cursor->bo  = armsoc_bo_new_with_dim(pARMSOC->dev,
				w + 2 * pad, h,
				0, 32, ARMSOC_BO_SCANOUT);

	if (!cursor->bo) {
		ERROR_MSG("HW cursor (standard): buffer allocation failed");
		free(cursor);
		return FALSE;
	}

	cursor->handle = armsoc_bo_handle(cursor->bo);

	if (!xf86_cursors_init(pScreen, w, h, HARDWARE_CURSOR_ARGB)) {
		ERROR_MSG("xf86_cursors_init() failed");
		if (drmModeRmFB(drmmode->fd, cursor->fb_id))
			ERROR_MSG("drmModeRmFB() failed");

		armsoc_bo_unreference(cursor->bo);
		free(cursor);
		return FALSE;
	}

	INFO_MSG("HW cursor initialized");
	drmmode->cursor = cursor;
	return TRUE;
}

Bool drmmode_cursor_init(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	INFO_MSG("HW cursor init()");

	switch (pARMSOC->drmmode_interface->cursor_api) {
	case HWCURSOR_API_PLANE:
		return drmmode_cursor_init_plane(pScreen);
	case HWCURSOR_API_STANDARD:
		return drmmode_cursor_init_standard(pScreen);
	case HWCURSOR_API_NONE:
		return FALSE;
	default:
		assert(0);
	}
}

void drmmode_cursor_fini(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct drmmode_rec *drmmode = drmmode_from_scrn(pScrn);
	struct drmmode_cursor_rec *cursor = drmmode->cursor;
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	if (!cursor)
		return;

	drmmode->cursor = NULL;
	xf86_cursors_fini(pScreen);
	if (pARMSOC->drmmode_interface->cursor_api == HWCURSOR_API_PLANE)
		drmModeRmFB(drmmode->fd, cursor->fb_id);
	armsoc_bo_unreference(cursor->bo);
	if (pARMSOC->drmmode_interface->cursor_api == HWCURSOR_API_PLANE)
		drmModeFreePlane(cursor->ovr);
	free(cursor);
}


#if 1 == ARMSOC_SUPPORT_GAMMA
static void
drmmode_gamma_set(xf86CrtcPtr crtc, CARD16 *red, CARD16 *green, CARD16 *blue,
		int size)
{
	struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;
	struct drmmode_rec *drmmode = drmmode_crtc->drmmode;
	int ret;

	ret = drmModeCrtcSetGamma(drmmode->fd, drmmode_crtc->crtc_id,
			size, red, green, blue);
	if (ret != 0) {
		xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
				"failed to set gamma: %s\n", strerror(-ret));
	}
}
#endif

static const xf86CrtcFuncsRec drmmode_crtc_funcs = {
		.dpms = drmmode_crtc_dpms,
		.set_mode_major = drmmode_set_mode_major,
		.set_cursor_position = drmmode_set_cursor_position,
		.show_cursor = drmmode_show_cursor,
		.hide_cursor = drmmode_hide_cursor,
		.load_cursor_argb = drmmode_load_cursor_argb,
#if 1 == ARMSOC_SUPPORT_GAMMA
		.gamma_set = drmmode_gamma_set,
#endif
};


static void
drmmode_crtc_init(ScrnInfoPtr pScrn, struct drmmode_rec *drmmode, int num)
{
	xf86CrtcPtr crtc;
	struct drmmode_crtc_private_rec *drmmode_crtc;

	TRACE_ENTER();

	crtc = xf86CrtcCreate(pScrn, &drmmode_crtc_funcs);
	if (crtc == NULL)
		return;

	drmmode_crtc = xnfcalloc(1, sizeof *drmmode_crtc);
	drmmode_crtc->crtc_id = drmmode->mode_res->crtcs[num];
	drmmode_crtc->drmmode = drmmode;
	drmmode_crtc->last_good_mode = NULL;

	INFO_MSG("Got CRTC: %d (id: %d)",
			num, drmmode_crtc->crtc_id);
	crtc->driver_private = drmmode_crtc;

	TRACE_EXIT();
	return;
}

static xf86OutputStatus
drmmode_output_detect(xf86OutputPtr output)
{
	/* go to the hw and retrieve a new output struct */
	struct drmmode_output_priv *drmmode_output = output->driver_private;
	struct drmmode_rec *drmmode = drmmode_output->drmmode;
	xf86OutputStatus status;
	drmModeFreeConnector(drmmode_output->connector);

	drmmode_output->connector =
			drmModeGetConnector(drmmode->fd,
					drmmode_output->output_id);

	switch (drmmode_output->connector->connection) {
	case DRM_MODE_CONNECTED:
		status = XF86OutputStatusConnected;
		break;
	case DRM_MODE_DISCONNECTED:
		status = XF86OutputStatusDisconnected;
		break;
	default:
	case DRM_MODE_UNKNOWNCONNECTION:
		status = XF86OutputStatusUnknown;
		break;
	}
	return status;
}

static Bool
drmmode_output_mode_valid(xf86OutputPtr output, DisplayModePtr mode)
{
	if (mode->type & M_T_DEFAULT)
		/* Default modes are harmful here. */
		return MODE_BAD;

	return MODE_OK;
}

static DisplayModePtr
drmmode_output_get_modes(xf86OutputPtr output)
{
	ScrnInfoPtr pScrn = output->scrn;
	struct drmmode_output_priv *drmmode_output = output->driver_private;
	drmModeConnectorPtr connector = drmmode_output->connector;
	struct drmmode_rec *drmmode = drmmode_output->drmmode;
	DisplayModePtr modes = NULL;
	drmModePropertyPtr prop;
	xf86MonPtr ddc_mon = NULL;
	int i;

	/* look for an EDID property */
	for (i = 0; i < connector->count_props; i++) {
		prop = drmModeGetProperty(drmmode->fd, connector->props[i]);
		if (!prop)
			continue;

		if ((prop->flags & DRM_MODE_PROP_BLOB) &&
		    !strcmp(prop->name, "EDID")) {
			if (drmmode_output->edid_blob)
				drmModeFreePropertyBlob(
						drmmode_output->edid_blob);
			drmmode_output->edid_blob =
					drmModeGetPropertyBlob(drmmode->fd,
						connector->prop_values[i]);
		}
		drmModeFreeProperty(prop);
	}

	if (drmmode_output->edid_blob)
		ddc_mon = xf86InterpretEDID(pScrn->scrnIndex,
				drmmode_output->edid_blob->data);

	if (ddc_mon) {
		xf86OutputSetEDID(output, ddc_mon);
		xf86SetDDCproperties(pScrn, ddc_mon);
	}

	DEBUG_MSG("count_modes: %d", connector->count_modes);

	/* modes should already be available */
	for (i = 0; i < connector->count_modes; i++) {
		DisplayModePtr mode = xnfalloc(sizeof(DisplayModeRec));

		drmmode_ConvertFromKMode(pScrn, &connector->modes[i], mode);
		modes = xf86ModesAdd(modes, mode);
	}
	return modes;
}

static void
drmmode_output_destroy(xf86OutputPtr output)
{
	struct drmmode_output_priv *drmmode_output = output->driver_private;
	int i;

	if (drmmode_output->edid_blob)
		drmModeFreePropertyBlob(drmmode_output->edid_blob);

	for (i = 0; i < drmmode_output->num_props; i++) {
		drmModeFreeProperty(drmmode_output->props[i].mode_prop);
		free(drmmode_output->props[i].atoms);
	}
	free(drmmode_output->props);

	for (i = 0; i < drmmode_output->connector->count_encoders; i++)
		drmModeFreeEncoder(drmmode_output->encoders[i]);

	free(drmmode_output->encoders);

	drmModeFreeConnector(drmmode_output->connector);
	free(drmmode_output);
	output->driver_private = NULL;
}

static void
drmmode_output_dpms(xf86OutputPtr output, int mode)
{
	struct drmmode_output_priv *drmmode_output = output->driver_private;
	drmModeConnectorPtr connector = drmmode_output->connector;
	drmModePropertyPtr prop;
	struct drmmode_rec *drmmode = drmmode_output->drmmode;
	int mode_id = -1, i;

	for (i = 0; i < connector->count_props; i++) {
		prop = drmModeGetProperty(drmmode->fd, connector->props[i]);
		if (!prop)
			continue;
		if ((prop->flags & DRM_MODE_PROP_ENUM) &&
		    !strcmp(prop->name, "DPMS")) {
			mode_id = connector->props[i];
			drmModeFreeProperty(prop);
			break;
		}
		drmModeFreeProperty(prop);
	}

	if (mode_id < 0)
		return;

	drmModeConnectorSetProperty(drmmode->fd, connector->connector_id,
			mode_id, mode);
}

static Bool
drmmode_property_ignore(drmModePropertyPtr prop)
{
	if (!prop)
		return TRUE;
	/* ignore blob prop */
	if (prop->flags & DRM_MODE_PROP_BLOB)
		return TRUE;
	/* ignore standard property */
	if (!strcmp(prop->name, "EDID") ||
			!strcmp(prop->name, "DPMS"))
		return TRUE;

	return FALSE;
}

static void
drmmode_output_create_resources(xf86OutputPtr output)
{
	struct drmmode_output_priv *drmmode_output = output->driver_private;
	drmModeConnectorPtr connector = drmmode_output->connector;
	struct drmmode_rec *drmmode = drmmode_output->drmmode;
	drmModePropertyPtr drmmode_prop;
	uint32_t value;
	int i, j, err;

	drmmode_output->props = calloc(connector->count_props,
					sizeof *connector->props);
	if (!drmmode_output->props)
		return;

	drmmode_output->num_props = 0;
	for (i = 0; i < connector->count_props; i++) {
		drmmode_prop = drmModeGetProperty(drmmode->fd,
			connector->props[i]);
		if (drmmode_property_ignore(drmmode_prop)) {
			drmModeFreeProperty(drmmode_prop);
			continue;
		}
		drmmode_output->props[drmmode_output->num_props].mode_prop =
				drmmode_prop;
		drmmode_output->props[drmmode_output->num_props].index = i;
		drmmode_output->num_props++;
	}

	for (i = 0; i < drmmode_output->num_props; i++) {
		struct drmmode_prop_rec *p = &drmmode_output->props[i];
		drmmode_prop = p->mode_prop;

		value = drmmode_output->connector->prop_values[p->index];

		if (drmmode_prop->flags & DRM_MODE_PROP_RANGE) {
			INT32 range[2];

			p->num_atoms = 1;
			p->atoms = calloc(p->num_atoms, sizeof *p->atoms);
			if (!p->atoms)
				continue;
			p->atoms[0] = MakeAtom(drmmode_prop->name,
						strlen(drmmode_prop->name),
						TRUE);
			range[0] = drmmode_prop->values[0];
			range[1] = drmmode_prop->values[1];
			err = RRConfigureOutputProperty(output->randr_output,
					p->atoms[0],
					FALSE, TRUE,
					drmmode_prop->flags &
						DRM_MODE_PROP_IMMUTABLE ?
							TRUE : FALSE,
					2, range);

			if (err != 0)
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
						"RRConfigureOutputProperty error, %d\n",
						err);

			err = RRChangeOutputProperty(output->randr_output,
					p->atoms[0],
					XA_INTEGER, 32, PropModeReplace, 1,
					&value, FALSE, FALSE);
			if (err != 0)
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
						"RRChangeOutputProperty error, %d\n",
						err);

		} else if (drmmode_prop->flags & DRM_MODE_PROP_ENUM) {
			p->num_atoms = drmmode_prop->count_enums + 1;
			p->atoms = calloc(p->num_atoms, sizeof *p->atoms);
			if (!p->atoms)
				continue;
			p->atoms[0] = MakeAtom(drmmode_prop->name,
						strlen(drmmode_prop->name),
						TRUE);
			for (j = 1; j <= drmmode_prop->count_enums; j++) {
				struct drm_mode_property_enum *e =
						&drmmode_prop->enums[j-1];
				p->atoms[j] = MakeAtom(e->name,
							strlen(e->name), TRUE);
			}
			err = RRConfigureOutputProperty(output->randr_output,
					p->atoms[0],
					FALSE, FALSE,
					drmmode_prop->flags &
						DRM_MODE_PROP_IMMUTABLE ?
							TRUE : FALSE,
					p->num_atoms - 1,
					(INT32 *)&p->atoms[1]);

			if (err != 0)
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
						"RRConfigureOutputProperty error, %d\n",
						err);

			for (j = 0; j < drmmode_prop->count_enums; j++)
				if (drmmode_prop->enums[j].value == value)
					break;
			/* there's always a matching value */
			err = RRChangeOutputProperty(output->randr_output,
					p->atoms[0],
					XA_ATOM, 32, PropModeReplace, 1,
					&p->atoms[j+1], FALSE, FALSE);
			if (err != 0)
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
						"RRChangeOutputProperty error, %d\n",
						err);
		}
	}
}

static Bool
drmmode_output_set_property(xf86OutputPtr output, Atom property,
		RRPropertyValuePtr value)
{
	struct drmmode_output_priv *drmmode_output = output->driver_private;
	struct drmmode_rec *drmmode = drmmode_output->drmmode;
	int i, ret;

	for (i = 0; i < drmmode_output->num_props; i++) {
		struct drmmode_prop_rec *p = &drmmode_output->props[i];

		if (p->atoms[0] != property)
			continue;

		if (p->mode_prop->flags & DRM_MODE_PROP_RANGE) {
			uint32_t val;

			if (value->type != XA_INTEGER || value->format != 32 ||
					value->size != 1)
				return FALSE;
			val = *(uint32_t *)value->data;

			ret = drmModeConnectorSetProperty(drmmode->fd,
					drmmode_output->output_id,
					p->mode_prop->prop_id, (uint64_t)val);

			if (ret)
				return FALSE;

			return TRUE;

		} else if (p->mode_prop->flags & DRM_MODE_PROP_ENUM) {
			Atom	atom;
			const char	*name;
			int		j;

			if (value->type != XA_ATOM ||
					value->format != 32 ||
					value->size != 1)
				return FALSE;

			memcpy(&atom, value->data, 4);
			name = NameForAtom(atom);
			if (name == NULL)
				return FALSE;

			/* search for matching name string, then
			 * set its value down
			 */
			for (j = 0; j < p->mode_prop->count_enums; j++) {
				if (!strcmp(p->mode_prop->enums[j].name,
						name)) {
					ret = drmModeConnectorSetProperty(
						drmmode->fd,
						drmmode_output->output_id,
						p->mode_prop->prop_id,
						p->mode_prop->enums[j].value);

					if (ret)
						return FALSE;

					return TRUE;
				}
			}
			return FALSE;
		}
	}
	return TRUE;
}

static Bool
drmmode_output_get_property(xf86OutputPtr output, Atom property)
{

	struct drmmode_output_priv *drmmode_output = output->driver_private;
	struct drmmode_rec *drmmode = drmmode_output->drmmode;
	uint32_t value;
	int err, i;

	if (output->scrn->vtSema) {
		drmModeFreeConnector(drmmode_output->connector);
		drmmode_output->connector =
				drmModeGetConnector(drmmode->fd,
						drmmode_output->output_id);
	}

	for (i = 0; i < drmmode_output->num_props; i++) {
		struct drmmode_prop_rec *p = &drmmode_output->props[i];
		if (p->atoms[0] != property)
			continue;

		value = drmmode_output->connector->prop_values[p->index];

		if (p->mode_prop->flags & DRM_MODE_PROP_RANGE) {
			err = RRChangeOutputProperty(output->randr_output,
					property, XA_INTEGER, 32,
					PropModeReplace, 1, &value,
					FALSE, FALSE);

			return !err;
		} else if (p->mode_prop->flags & DRM_MODE_PROP_ENUM) {
			int		j;

			/* search for matching name string, then set
			 * its value down
			 */
			for (j = 0; j < p->mode_prop->count_enums; j++) {
				if (p->mode_prop->enums[j].value == value)
					break;
			}

			err = RRChangeOutputProperty(output->randr_output,
						property,
						XA_ATOM, 32, PropModeReplace, 1,
						&p->atoms[j+1], FALSE, FALSE);

			return !err;
		}
	}

	return FALSE;
}

static const xf86OutputFuncsRec drmmode_output_funcs = {
		.create_resources = drmmode_output_create_resources,
		.dpms = drmmode_output_dpms,
		.detect = drmmode_output_detect,
		.mode_valid = drmmode_output_mode_valid,
		.get_modes = drmmode_output_get_modes,
		.set_property = drmmode_output_set_property,
		.get_property = drmmode_output_get_property,
		.destroy = drmmode_output_destroy
};

const char *output_names[] = { "None",
		"VGA",
		"DVI-I",
		"DVI-D",
		"DVI-A",
		"Composite",
		"SVIDEO",
		"LVDS",
		"CTV",
		"DIN",
		"DP",
		"HDMI",
		"HDMI",
		"TV",
		"eDP",
};
#define NUM_OUTPUT_NAMES (sizeof(output_names) / sizeof(output_names[0]))

static void
drmmode_output_init(ScrnInfoPtr pScrn, struct drmmode_rec *drmmode, int num)
{
	xf86OutputPtr output;
	drmModeConnectorPtr connector;
	drmModeEncoderPtr *encoders = NULL;
	struct drmmode_output_priv *drmmode_output;
	char name[32];
	int i;

	TRACE_ENTER();

	connector = drmModeGetConnector(drmmode->fd, drmmode->mode_res->connectors[num]);
	if (!connector)
		goto exit;

	encoders = calloc(sizeof(drmModeEncoderPtr), connector->count_encoders);
	if (!encoders)
		goto free_connector_exit;

	for (i = 0; i < connector->count_encoders; i++) {
		encoders[i] = drmModeGetEncoder(drmmode->fd, connector->encoders[i]);
		if (!encoders[i])
			goto free_encoders_exit;
	}

	if (connector->connector_type >= NUM_OUTPUT_NAMES)
		snprintf(name, 32, "Unknown%d-%d", connector->connector_type, connector->connector_type_id);
	else
		snprintf(name, 32, "%s-%d", output_names[connector->connector_type], connector->connector_type_id);

	output = xf86OutputCreate(pScrn, &drmmode_output_funcs, name);
	if (!output)
		goto free_encoders_exit;

	drmmode_output = calloc(1, sizeof *drmmode_output);
	if (!drmmode_output) {
		xf86OutputDestroy(output);
		goto free_encoders_exit;
	}

	drmmode_output->output_id = drmmode->mode_res->connectors[num];
	drmmode_output->connector = connector;
	drmmode_output->encoders = encoders;
	drmmode_output->drmmode = drmmode;

	output->mm_width = connector->mmWidth;
	output->mm_height = connector->mmHeight;
	output->driver_private = drmmode_output;

	/*
	 * Determine which crtcs are supported by all the encoders which
	 * are valid for the connector of this output.
	 */
	output->possible_crtcs = 0xffffffff;
	for (i = 0; i < connector->count_encoders; i++)
		output->possible_crtcs &= encoders[i]->possible_crtcs;
	/*
	 * output->possible_crtcs is a bitmask arranged by index of crtcs for this screen while
	 * encoders->possible_crtcs covers all crtcs supported by the drm. If we have selected
	 * one crtc per screen, it must be at index 0.
	 */
	if (ARMSOCPTR(pScrn)->crtcNum >= 0)
		output->possible_crtcs = (output->possible_crtcs >> (ARMSOCPTR(pScrn)->crtcNum)) & 1;

	output->possible_clones = 0; /* set after all outputs initialized */
	output->interlaceAllowed = TRUE;
	goto exit;

free_encoders_exit:
	for (i = 0; i < connector->count_encoders; i++)
		drmModeFreeEncoder(encoders[i]);

free_connector_exit:
	drmModeFreeConnector(connector);

exit:
	TRACE_EXIT();
	return;

}
static void
drmmode_clones_init(ScrnInfoPtr pScrn, struct drmmode_rec *drmmode)
{
	int i;
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);

	/* For each output generate enc_mask, a mask of encoders present,
	 * and enc_clones, a mask of possible clone encoders
	 */
	for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];
		struct drmmode_output_priv *drmmode_output = output->driver_private;
		int j;

		drmmode_output->enc_clones = 0xffffffff;
		drmmode_output->enc_mask = 0;

		for (j = 0; j < drmmode_output->connector->count_encoders; j++) {
			int k;

			/* set index ordered mask of encoders on this output */
			for (k = 0; k < drmmode->mode_res->count_encoders; k++) {
				if (drmmode->mode_res->encoders[k] == drmmode_output->encoders[j]->encoder_id)
					drmmode_output->enc_mask |= (1 << k);
			}
			/* set mask for encoder clones possible with all encoders on this output */
			drmmode_output->enc_clones &= drmmode_output->encoders[j]->possible_clones;
		}
	}

	/* Output j is a possible clone of output i if the enc_mask for j matches the enc_clones for i */
	for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];
		struct drmmode_output_priv *drmmode_output = output->driver_private;
		int j;

		output->possible_clones = 0;
		if (drmmode_output->enc_clones == 0)
			continue;

		for (j = 0; j < xf86_config->num_output; j++) {
			struct drmmode_output_priv *clone = xf86_config->output[j]->driver_private;

			if ((i != j) &&
				(clone->enc_mask != 0) &&
				(drmmode_output->enc_clones == clone->enc_mask))

				output->possible_clones |= (1 << j);
		}
	}
}

void set_scanout_bo(ScrnInfoPtr pScrn, struct armsoc_bo *bo)
{
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	/* It had better have a framebuffer if we're scanning it out */
	assert(armsoc_bo_get_fb(bo));

	pARMSOC->scanout = bo;
}

static Bool resize_scanout_bo(ScrnInfoPtr pScrn, int width, int height)
{
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	ScreenPtr pScreen = pScrn->pScreen;
	uint32_t pitch;

	TRACE_ENTER();
	DEBUG_MSG("Resize: %dx%d", width, height);

	pScrn->virtualX = width;
	pScrn->virtualY = height;

	if ((width != armsoc_bo_width(pARMSOC->scanout))
	      || (height != armsoc_bo_height(pARMSOC->scanout))
	      || (pScrn->bitsPerPixel != armsoc_bo_bpp(pARMSOC->scanout))) {
		struct armsoc_bo *new_scanout;

		/* allocate new scanout buffer */
		new_scanout = armsoc_bo_new_with_dim(pARMSOC->dev,
				width, height,
				pScrn->depth, pScrn->bitsPerPixel,
				ARMSOC_BO_SCANOUT);
		if (!new_scanout) {
			/* Try to use the previous buffer if the new resolution
			 * is smaller than the one on buffer creation
			 */
			DEBUG_MSG(
					"allocate new scanout buffer failed - resizing existing bo");
			/* Remove the old fb from the bo */
			if (armsoc_bo_rm_fb(pARMSOC->scanout))
				return FALSE;

			/* Resize the bo */
			if (armsoc_bo_resize(pARMSOC->scanout, width, height)) {
				armsoc_bo_clear(pARMSOC->scanout);
				if (armsoc_bo_add_fb(pARMSOC->scanout))
					ERROR_MSG(
							"Failed to add framebuffer to the existing scanout buffer");
				return FALSE;
			}

			/* Add new fb to the bo */
			if (armsoc_bo_clear(pARMSOC->scanout))
				return FALSE;

			if (armsoc_bo_add_fb(pARMSOC->scanout)) {
				ERROR_MSG(
						"Failed to add framebuffer to the existing scanout buffer");
				return FALSE;
			}

			pitch = armsoc_bo_pitch(pARMSOC->scanout);
		} else {
			DEBUG_MSG("allocated new scanout buffer okay");
			pitch = armsoc_bo_pitch(new_scanout);
			/* clear new BO and add FB */
			if (armsoc_bo_clear(new_scanout)) {
				armsoc_bo_unreference(new_scanout);
				return FALSE;
			}

			if (armsoc_bo_add_fb(new_scanout)) {
				ERROR_MSG(
						"Failed to add framebuffer to the new scanout buffer");
				armsoc_bo_unreference(new_scanout);
				return FALSE;
			}

			/* Handle dma_buf fd that may be attached to old bo */
			if (armsoc_bo_has_dmabuf(pARMSOC->scanout)) {
				int res;

				armsoc_bo_clear_dmabuf(pARMSOC->scanout);
				res = armsoc_bo_set_dmabuf(new_scanout);
				if (res) {
					ERROR_MSG(
							"Unable to attach dma_buf fd to new scanout buffer - %d (%s)\n",
							res, strerror(res));
					armsoc_bo_unreference(new_scanout);
					return FALSE;
				}
			}
			/* delete old scanout buffer */
			armsoc_bo_unreference(pARMSOC->scanout);
			/* use new scanout buffer */
			set_scanout_bo(pScrn, new_scanout);
		}
		pScrn->displayWidth = pitch / ((pScrn->bitsPerPixel + 7) / 8);
	} else
		pitch = armsoc_bo_pitch(pARMSOC->scanout);

	if (pScreen && pScreen->ModifyPixmapHeader) {
		PixmapPtr rootPixmap = pScreen->GetScreenPixmap(pScreen);

		pScreen->ModifyPixmapHeader(rootPixmap,
				pScrn->virtualX, pScrn->virtualY,
				pScrn->depth, pScrn->bitsPerPixel, pitch,
				armsoc_bo_map(pARMSOC->scanout));

		/* Bump the serial number to ensure that all existing DRI2
		 * buffers are invalidated.
		 *
		 * This is particularly required for when the resolution is
		 * changed and then reverted to the original size without a
		 * DRI2 client/s getting a new buffer. Without this, the
		 * drawable is the same size and serial number so the old
		 * DRI2Buffer will be returned, even though the backing buffer
		 * has been deleted.
		 */
		rootPixmap->drawable.serialNumber = NEXT_SERIAL_NUMBER;
	}
	TRACE_EXIT();
	return TRUE;
}

static Bool
drmmode_xf86crtc_resize(ScrnInfoPtr pScrn, int width, int height)
{
	int i;
	xf86CrtcConfigPtr xf86_config;

	TRACE_ENTER();
	if (!resize_scanout_bo(pScrn, width, height))
		return FALSE;

	/* Framebuffer needs to be reset on all CRTCs, not just
	 * those that have repositioned */
	xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
	for (i = 0; i < xf86_config->num_crtc; i++) {
		xf86CrtcPtr crtc = xf86_config->crtc[i];

		if (!crtc->enabled)
			continue;

		drmmode_set_mode_major(crtc, &crtc->mode,
				crtc->rotation, crtc->x, crtc->y);
	}

	TRACE_EXIT();
	return TRUE;
}

static const xf86CrtcConfigFuncsRec drmmode_xf86crtc_config_funcs = {
		.resize = drmmode_xf86crtc_resize
};


Bool drmmode_pre_init(ScrnInfoPtr pScrn, int fd, int cpp)
{
	struct drmmode_rec *drmmode;
	int i;

	TRACE_ENTER();

	drmmode = calloc(1, sizeof(*drmmode));
	if (!drmmode)
		return FALSE;

	drmmode->fd = fd;

	xf86CrtcConfigInit(pScrn, &drmmode_xf86crtc_config_funcs);


	drmmode->cpp = cpp;
	drmmode->mode_res = drmModeGetResources(drmmode->fd);
	if (!drmmode->mode_res) {
		free(drmmode);
		return FALSE;
	} else {
		DEBUG_MSG("Got KMS resources");
		DEBUG_MSG("  %d connectors, %d encoders",
				drmmode->mode_res->count_connectors,
				drmmode->mode_res->count_encoders);
		DEBUG_MSG("  %d crtcs, %d fbs",
				drmmode->mode_res->count_crtcs,
				drmmode->mode_res->count_fbs);
		DEBUG_MSG("  %dx%d minimum resolution",
				drmmode->mode_res->min_width,
				drmmode->mode_res->min_height);
		DEBUG_MSG("  %dx%d maximum resolution",
				drmmode->mode_res->max_width,
				drmmode->mode_res->max_height);
	}
	xf86CrtcSetSizeRange(pScrn, 320, 200, drmmode->mode_res->max_width,
			drmmode->mode_res->max_height);

	if (ARMSOCPTR(pScrn)->crtcNum == -1) {
		INFO_MSG("Adding all CRTCs");
		for (i = 0; i < drmmode->mode_res->count_crtcs; i++)
			drmmode_crtc_init(pScrn, drmmode, i);
	} else if (ARMSOCPTR(pScrn)->crtcNum < drmmode->mode_res->count_crtcs) {
		drmmode_crtc_init(pScrn, drmmode, ARMSOCPTR(pScrn)->crtcNum);
	} else {
		ERROR_MSG(
				"Specified more Screens in xorg.conf than there are DRM CRTCs");
		return FALSE;
	}

	if (ARMSOCPTR(pScrn)->crtcNum != -1) {
		if (ARMSOCPTR(pScrn)->crtcNum <
				drmmode->mode_res->count_connectors)
			drmmode_output_init(pScrn,
					drmmode, ARMSOCPTR(pScrn)->crtcNum);
		else
			return FALSE;
	} else {
		for (i = 0; i < drmmode->mode_res->count_connectors; i++)
			drmmode_output_init(pScrn, drmmode, i);
	}
	drmmode_clones_init(pScrn, drmmode);

	xf86InitialConfiguration(pScrn, TRUE);

	TRACE_EXIT();

	return TRUE;
}

void
drmmode_adjust_frame(ScrnInfoPtr pScrn, int x, int y)
{
	xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
	xf86OutputPtr output = config->output[config->compat_output];
	xf86CrtcPtr crtc = output->crtc;

	if (!crtc || !crtc->enabled)
		return;

	drmmode_set_mode_major(crtc, &crtc->mode, crtc->rotation, x, y);
}

/*
 * Page Flipping
 */

static void
page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
		unsigned int tv_usec, void *user_data)
{
	ARMSOCDRI2SwapComplete(user_data);
}

static drmEventContext event_context = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.page_flip_handler = page_flip_handler,
};

int
drmmode_page_flip(DrawablePtr draw, uint32_t fb_id, void *priv)
{
	ScreenPtr pScreen = draw->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
	struct drmmode_crtc_private_rec *crtc = config->crtc[0]->driver_private;
	struct drmmode_rec *mode = crtc->drmmode;
	int ret, i, failed = 0, num_flipped = 0;
	unsigned int flags = 0;

	if (pARMSOC->drmmode_interface->use_page_flip_events)
		flags |= DRM_MODE_PAGE_FLIP_EVENT;

	/* if we can flip, we must be fullscreen.. so flip all CRTC's.. */
	for (i = 0; i < config->num_crtc; i++) {
		crtc = config->crtc[i]->driver_private;

		if (!config->crtc[i]->enabled)
			continue;

		ret = drmModePageFlip(mode->fd, crtc->crtc_id,
				fb_id, flags, priv);
		if (ret) {
			xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
					"flip queue failed: %s\n",
					strerror(errno));
			failed = 1;
		} else
			num_flipped += 1;
	}

	if (failed)
		return -(num_flipped + 1);
	else
		return num_flipped;
}

/*
 * Hot Plug Event handling:
 * TODO: MIDEGL-1441: Do we need to keep this handler, which
 * Rob originally wrote?
 */
static void
drmmode_handle_uevents(int fd, void *closure)
{
	ScrnInfoPtr pScrn = closure;
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct drmmode_rec *drmmode = drmmode_from_scrn(pScrn);
	struct udev_device *dev;
	const char *hotplug;
	struct stat s;
	dev_t udev_devnum;

	dev = udev_monitor_receive_device(drmmode->uevent_monitor);
	if (!dev)
		return;

	/*
	 * Check to make sure this event is directed at our
	 * device (by comparing dev_t values), then make
	 * sure it's a hotplug event (HOTPLUG=1)
	 */
	udev_devnum = udev_device_get_devnum(dev);
	if (fstat(pARMSOC->drmFD, &s)) {
		ERROR_MSG("fstat failed: %s", strerror(errno));
		udev_device_unref(dev);
		return;
	}

	hotplug = udev_device_get_property_value(dev, "HOTPLUG");

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hotplug=%s, match=%d\n", hotplug,
			!memcmp(&s.st_rdev, &udev_devnum, sizeof(dev_t)));

	if (memcmp(&s.st_rdev, &udev_devnum, sizeof(dev_t)) == 0 &&
			hotplug && atoi(hotplug) == 1) {
		RRGetInfo(xf86ScrnToScreen(pScrn), TRUE);
	}
	udev_device_unref(dev);
}

static void
drmmode_uevent_init(ScrnInfoPtr pScrn)
{
	struct drmmode_rec *drmmode = drmmode_from_scrn(pScrn);
	struct udev *u;
	struct udev_monitor *mon;

	TRACE_ENTER();

	u = udev_new();
	if (!u)
		return;
	mon = udev_monitor_new_from_netlink(u, "udev");
	if (!mon) {
		udev_unref(u);
		return;
	}

	if (udev_monitor_filter_add_match_subsystem_devtype(mon,
			"drm",
			"drm_minor") < 0 ||
			udev_monitor_enable_receiving(mon) < 0) {
		udev_monitor_unref(mon);
		udev_unref(u);
		return;
	}

	drmmode->uevent_handler =
			xf86AddGeneralHandler(udev_monitor_get_fd(mon),
					drmmode_handle_uevents, pScrn);

	drmmode->uevent_monitor = mon;

	TRACE_EXIT();
}

static void
drmmode_uevent_fini(ScrnInfoPtr pScrn)
{
	struct drmmode_rec *drmmode = drmmode_from_scrn(pScrn);

	TRACE_ENTER();

	if (drmmode->uevent_handler) {
		struct udev *u = udev_monitor_get_udev(drmmode->uevent_monitor);
		xf86RemoveGeneralHandler(drmmode->uevent_handler);

		udev_monitor_unref(drmmode->uevent_monitor);
		udev_unref(u);
	}

	TRACE_EXIT();
}

static void
drmmode_wakeup_handler(pointer data, int err, pointer p)
{
	ScrnInfoPtr pScrn = data;
	struct drmmode_rec *drmmode;
	fd_set *read_mask = p;

	if (pScrn == NULL || err < 0)
		return;

	drmmode = drmmode_from_scrn(pScrn);

	if (FD_ISSET(drmmode->fd, read_mask))
		drmHandleEvent(drmmode->fd, &event_context);
}

void
drmmode_wait_for_event(ScrnInfoPtr pScrn)
{
	struct drmmode_rec *drmmode = drmmode_from_scrn(pScrn);
	drmHandleEvent(drmmode->fd, &event_context);
}

void
drmmode_screen_init(ScrnInfoPtr pScrn)
{
	struct drmmode_rec *drmmode = drmmode_from_scrn(pScrn);

	drmmode_uevent_init(pScrn);

	AddGeneralSocket(drmmode->fd);

	/* Register a wakeup handler to get informed on DRM events */
	RegisterBlockAndWakeupHandlers((BlockHandlerProcPtr)NoopDDA,
			drmmode_wakeup_handler, pScrn);
}

void
drmmode_screen_fini(ScrnInfoPtr pScrn)
{
	drmmode_uevent_fini(pScrn);
}
