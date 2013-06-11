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

#include "../drmmode_driver.h"

/* Cursor dimensions
 * Technically we probably don't have any size limit.. since we
 * are just using an overlay... but xserver will always create
 * cursor images in the max size, so don't use width/height values
 * that are too big
 */
/* width */
#define CURSORW   (64)
/* height */
#define CURSORH   (64)
/* Padding added down each side of cursor image */
#define CURSORPAD (0)

/* Optional function */
static int init_plane_for_cursor(int drm_fd, uint32_t plane_id)
{
	return 0;
}

static void set_cursor_image(xf86CrtcPtr crtc, uint32_t *d, CARD32 *s)
{
	/* provide a method of setting the cursor image here */
}

struct drmmode_interface template_interface = {
	0x00000000 /* dumb_scanout_flags */,
	0x00000000 /* dumb_no_scanout_flags */,
	1 /* use_page_flip_events */,
	CURSORW               /* cursor width */,
	CURSORH               /* cursor_height */,
	CURSORPAD             /* cursor padding */,
	init_plane_for_cursor /* init_plane_for_cursor */,
	set_cursor_image      /* set cursor image */,
};

struct drmmode_interface *drmmode_interface_get_implementation(int drm_fd)
{
	return &template_interface;
}

