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

struct drmmode_interface {
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

	/* (Mandatory) Set the cursor image from an ARGB image
	 *
	 * If the cursor image is ARGB this is a straight copy, otherwise
	 * it must perform any necessary conversion from ARGB to the
	 * cursor format.
	 *
	 * @param       crtc  The CRTC in use
	 * @param [out] d     Pointer to the destination cursor image
	 * @param [in]  s     Pointer to the source for the cursor image
	 */
	void (*set_cursor_image)(xf86CrtcPtr crtc, uint32_t *d, CARD32 *s);

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
};

struct drmmode_interface *drmmode_interface_get_implementation(int drm_fd);

#endif
