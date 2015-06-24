/*
 * Copyright © 2013 ARM Limited.
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
 */

#ifndef DRMMODE_DRIVER_H
#define DRMMODE_DRIVER_H

#include <stdint.h>

#include "xorg-server.h"
#include "xf86Crtc.h"
#include "armsoc_dumb.h"

#define ARMSOC_GEM_DOMAIN_CPU   0x01
#define ARMSOC_GEM_DOMAIN_MALI  0x02

enum hwcursor_api {
	HWCURSOR_API_PLANE = 0,
	HWCURSOR_API_STANDARD = 1,
};

/*
 * Generic GEM set domain information used to abstract custom GEM set domain
 * for every DRM driver.
 */
struct armsoc_gem_set_domain {
	uint32_t handle;
	uint32_t write_domain;
};

enum armsoc_drm_cache_op_control {
	ARMSOC_DRM_CACHE_OP_START,
	ARMSOC_DRM_CACHE_OP_FINISH,
	ARMSOC_DRM_CACHE_OP_COUNT,
};

struct drmmode_interface {
	/* Must match name used in the kernel driver */
	const char *driver_name;

	/* Boolean value indicating whether DRM page flip events should
	 * be requested and waited for during DRM_IOCTL_MODE_PAGE_FLIP.
	 */
	int use_page_flip_events;

	/* The cursor width */
	int cursor_width;

	/* The cursor height */
	int cursor_height;

	/* A padding column of pixels of this width is added to either
	 * side of the image
	 */
	int cursor_padding;

	/* This specifies whether the DRM implements HW cursor support
	 * using planes or the standard HW cursor API using drmModeSetCursor()
	 * and drmModeMoveCursor().
	 */
	enum hwcursor_api cursor_api;

	/* (Optional) Initialize the given plane for use as a hardware cursor.
	 *
	 * This function should do any initialization necessary, for example
	 * setting the z-order on the plane to appear above all other layers.
	 *
	 * @param drm_fd   The DRM device file
	 * @param plane_id The plane to initialize
	 * @return 0 on success, non-zero on failure
	 */
	int (*init_plane_for_cursor)(int drm_fd, uint32_t plane_id);

	/* Boolean value indicating whether the DRM supports
	 * vblank timestamp query
	 */
	int vblank_query_supported;

	/* (Mandatory) Create new gem object
	 *
	 * A driver specific ioctl() is usually needed to create GEM objects
	 * with particular features such as contiguous memory, uncached, etc...
	 *
	 * @param       fd             DRM device file descriptor
	 * @param       create_gem     generic GEM description
	 * @return 0 on success, non-zero on failure
	 */
	int (*create_custom_gem)(int fd, struct armsoc_create_gem *create_gem);

	/* (Optional) Signal to the kernel that cache control operations are
	 * about to start of have just finished.
	 *
	 * A driver specific ioctl() to trigger the necessary cache flushes or
	 * invalidations before the CPU can access the buffer.
	 *
	 * @param       fd               DRM device file descriptor
	 * @return 0 on success, non-zero on failure
	 */
	int (*cache_ops_control)(int fd, enum armsoc_drm_cache_op_control op);

	/* (Optional) Set the domain of a gem object
	 *
	 * A driver specific ioctl() to trigger the necessary cache flushes or
	 * invalidations before the CPU can access the buffer.
	 *
	 * @param       fd               DRM device file descriptor
	 * @param       gem_set_domain   generic GEM set domain information
	 * @return 0 on success, non-zero on failure
	 */
	int (*gem_set_domain)(int fd, struct armsoc_gem_set_domain gsd);
};

extern struct drmmode_interface exynos_interface;
extern struct drmmode_interface pl111_interface;
extern struct drmmode_interface meson_interface;

#endif
