/*
 * Copyright (C) 2014 Endless Mobile
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

#include "../drmmode_driver.h"

#include "meson_drm.h"
#include <errno.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

/* Cursor dimensions
 * Technically we probably don't have any size limit.. since we
 * are just using an overlay... but xserver will always create
 * cursor images in the max size, so don't use width/height values
 * that are too big
 */
#define CURSORW  (64)
#define CURSORH  (64)

#define CURSORPAD  (0)

#define ALIGN(val, align)	(((val) + (align) - 1) & ~((align) - 1))

static int create_custom_gem(int fd, struct armsoc_create_gem *create_gem)
{
	struct drm_meson_gem_create_with_ump create_meson;
	size_t pitch;
	int ret;

	assert((create_gem->buf_type == ARMSOC_BO_SCANOUT) ||
	       (create_gem->buf_type == ARMSOC_BO_NON_SCANOUT));

	/* make pitch a multiple of 64 bytes for best performance */
	pitch = ALIGN(create_gem->width * ((create_gem->bpp + 7) / 8), 64);
	create_meson.size = create_gem->height * pitch;
	create_meson.flags = 0;

	if (create_gem->buf_type == ARMSOC_BO_SCANOUT)
		create_meson.flags |= DRM_MESON_GEM_CREATE_WITH_UMP_FLAG_SCANOUT;
	else
		create_meson.flags |= DRM_MESON_GEM_CREATE_WITH_UMP_FLAG_TEXTURE;

	ret = drmIoctl(fd, DRM_IOCTL_MESON_GEM_CREATE_WITH_UMP, &create_meson);
	if (ret)
		return ret;

	/* Convert custom meson ioctl to generic create_gem */
	create_gem->handle = create_meson.handle;
	create_gem->pitch = pitch;
	create_gem->size = create_meson.size;

	return 0;
}

static int cache_ops_control(int fd, enum armsoc_drm_cache_op_control op)
{
	struct drm_meson_cache_operations_control coc;
	int ret;

	switch (op) {
		case ARMSOC_DRM_CACHE_OP_START:
			coc.op = DRM_MESON_CACHE_OP_START;
			break;
		case ARMSOC_DRM_CACHE_OP_FINISH:
			coc.op = DRM_MESON_CACHE_OP_FINISH;
			break;
		case ARMSOC_DRM_CACHE_OP_COUNT:
			return -EINVAL;
	}

	ret = drmIoctl(fd, DRM_IOCTL_MESON_CACHE_OPERATIONS_CONTROL, &coc);
	if (ret < 0) {
		ErrorF("cache_operations_control ioctl failed:%s\n",
		       strerror(errno));
		return ret;
	}

	return 0;
}

static int gem_set_domain(int fd, struct armsoc_gem_set_domain gsd)
{
	struct drm_meson_gem_set_domain mgsd;
	int ret;

	mgsd.handle = gsd.handle;
	mgsd.write_domain = gsd.write_domain;
	ret = drmIoctl(fd, DRM_IOCTL_MESON_GEM_SET_DOMAIN, &mgsd);
	if (ret < 0) {
		ErrorF("gem_set_domain(CPU) failed: bo %d: %s\n", gsd.handle,
		       strerror(errno));
		return ret;
	}

	return 0;
}

struct drmmode_interface meson_interface = {
	"meson",
	1                     /* use_page_flip_events */,
	CURSORW               /* cursor width */,
	CURSORH               /* cursor_height */,
	CURSORPAD             /* cursor padding */,
	HWCURSOR_API_STANDARD /* cursor_api */,
	NULL                  /* init_plane_for_cursor */,
	0                     /* vblank_query_supported */,
	create_custom_gem     /* create_custom_gem */,
	cache_ops_control     /* cache_ops_control */,
	gem_set_domain        /* gem_set_domain */,
};
