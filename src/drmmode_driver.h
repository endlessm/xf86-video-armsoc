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

struct drmmode_interface {
	/* Flags value to pass to DRM_IOCTL_MODE_CREATE_DUMB to allocate a scanout-capable
	 * buffer. A buffer allocated with these flags must be able to be wrapped in a
	 * DRM framebuffer (via DRM_IOCTL_MODE_ADDFB or DRM_IOCTL_MODE_ADDFB2).
	 */
	uint32_t dumb_scanout_flags;

	/* Flags value to pass to DRM_IOCTL_MODE_CREATE_DUMB to allocate a
	 * non-scanout-capable buffer. It is acceptable for the driver to create a
	 * scanout-capable buffer when given this flag, this flag is used to give the
	 * option of preserving scarce scanout-capable memory if applicable.
	 */
	uint32_t dumb_no_scanout_flags;

	/* Boolean value indicating whether DRM page flip events should be requested and
	 * waited for during DRM_IOCTL_MODE_PAGE_FLIP.
	 */
	int use_page_flip_events;
};

struct drmmode_interface *drmmode_interface_get_implementation(int drm_fd);

#endif
