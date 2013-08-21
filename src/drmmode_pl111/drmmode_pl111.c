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
#include <stddef.h>
#include <xf86drm.h>

/* Cursor dimensions
 * Technically we probably don't have any size limit.. since we
 * are just using an overlay... but xserver will always create
 * cursor images in the max size, so don't use width/height values
 * that are too big
 */
#define CURSORW   (64)
#define CURSORH   (64)
/* Padding added down each side of cursor image */
#define CURSORPAD (0)

/* un-comment to enable cursor format conversion debugging.*/
/* #define ARGB_LBBP_CONVERSION_DEBUG */

#define LBBP_BACKGROUND  (0x0)
#define LBBP_FOREGROUND  (0x1)
#define LBBP_TRANSPARENT (0x2)
#define LBBP_INVERSE     (0x3)

#define ARGB_ALPHA (0xff000000)
#define ARGB_RGB (~ARGB_ALPHA)

#define LBBP_WORDS_PER_LINE        (4)
#define LBBP_PIXELS_PER_WORD       (16)

#define PL111_SCANOUT_FLAGS   0x00000001
#define PL111_NON_SCANOUT_FLAGS   0x00000000

/* shift required to locate pixel into the correct position in
 * a cursor LBBP word, indexed by x mod 16.
 */
const unsigned char x_mod_16_to_value_shift[LBBP_PIXELS_PER_WORD] = {
	6, 4, 2, 0, 14, 12, 10, 8, 22, 20, 18, 16, 30, 28, 26, 24
};

/* Pack the pixel value into its correct position in the buffer as specified
 * for LBBP */
static inline void
set_lbbp_pixel(uint32_t *buffer, unsigned int x, unsigned int y,
				uint32_t value)
{
	uint32_t *p;
	uint32_t shift;

	assert((x < CURSORW) && (y < CURSORH));

	shift = x_mod_16_to_value_shift[x % LBBP_PIXELS_PER_WORD];

	/* Get the word containing this pixel */
	p = buffer + (x >> LBBP_WORDS_PER_LINE) + (y << 2);

	/* Clear data for this pixel in the buffer and apply the new data */
	*p &= ~(LBBP_INVERSE << shift);
	*p |= value << shift;
}

/*
 * The PL111 hardware cursor supports only LBBP which is a 2bpp format but
 * there are no drm fourcc formats that are compatible with this so instead
 * the PL111 DRM reports (to DRM core) that it supports only
 * DRM_FORMAT_ARGB8888 and expects the DDX to supply an LBPP image in the
 * first 1/16th of the buffer, the rest being unused.
 * Ideally we would want to receive the image in this format from X, but
 *  currently the X cursor image is 32bpp ARGB so we need to convert
 *  to LBBP here.
 */
static void set_cursor_image(xf86CrtcPtr crtc, uint32_t *d, CARD32 *s)
{
#ifdef ARGB_LBBP_CONVERSION_DEBUG
	/* Add 1 on width to insert trailing NULL */
	char string_cursor[CURSORW+1];
#endif /* ARGB_LBBP_CONVERSION_DEBUG */
	unsigned int x;
	unsigned int y;

	for (y = 0; y < CURSORH ; y++) {
		for (x = 0; x < CURSORW ; x++) {
			uint32_t value = LBBP_TRANSPARENT;
			/* If pixel visible foreground/background */
			if ((*s & ARGB_ALPHA) != 0) {
				/* Any color set then just convert to
				 * foreground for now
				 */
				if ((*s & ARGB_RGB) != 0)
					value = LBBP_FOREGROUND;
				else
					value = LBBP_BACKGROUND;
			}
#ifdef ARGB_LBBP_CONVERSION_DEBUG
			if (value == LBBP_TRANSPARENT)
				string_cursor[x] = 'T';
			else if (value == LBBP_FOREGROUND)
				string_cursor[x] = 'F';
			else if (value == LBBP_INVERSE)
				string_cursor[x] = 'I';
			else
				string_cursor[x] = 'B';

#endif /* ARGB_LBBP_CONVERSION_DEBUG */
			set_lbbp_pixel(d, x, y, value);
			++s;
		}
#ifdef ARGB_LBBP_CONVERSION_DEBUG
		string_cursor[CURSORW] = '\0';
		xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "%s\n",
					string_cursor);
#endif /* ARGB_LBBP_CONVERSION_DEBUG */
	}
}

/* TODO MIDEGL-1718: this should be included from kernel headers when pl111 is mainline */
struct drm_pl111_gem_create {
	uint32_t height;
	uint32_t width;
	uint32_t bpp;
	uint32_t flags;
	/* handle, pitch, size will be returned */
	uint32_t handle;
	uint32_t pitch;
	uint64_t size;
};

#define DRM_PL111_GEM_CREATE		0x00
#define DRM_IOCTL_PL111_GEM_CREATE	DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_PL111_GEM_CREATE, struct drm_pl111_gem_create)
/***************************************************************************/

static int create_custom_gem(int fd, struct armsoc_create_gem *create_gem)
{
	struct drm_pl111_gem_create create_pl111;
	int ret;

	memset(&create_pl111, 0, sizeof(create_pl111));
	create_pl111.height = create_gem->height;
	create_pl111.width = create_gem->width;
	create_pl111.bpp = create_gem->bpp;

	assert((create_gem->buf_type == ARMSOC_BO_SCANOUT) ||
			(create_gem->buf_type == ARMSOC_BO_NON_SCANOUT));
	if (create_gem->buf_type == ARMSOC_BO_SCANOUT)
		create_pl111.flags = PL111_SCANOUT_FLAGS;
	else
		create_pl111.flags = PL111_NON_SCANOUT_FLAGS;

	ret = drmIoctl(fd, DRM_IOCTL_PL111_GEM_CREATE, &create_pl111);
	if (ret)
		return ret;

	/* Convert custom create_pl111 to generic create_gem */
	create_gem->height = create_pl111.height;
	create_gem->width = create_pl111.width;
	create_gem->bpp = create_pl111.bpp;
	create_gem->handle = create_pl111.handle;
	create_gem->pitch = create_pl111.pitch;
	create_gem->size = create_pl111.size;

	return 0;
}

struct drmmode_interface pl111_interface = {
	0                     /* use_page_flip_events */,
	CURSORW               /* cursor width */,
	CURSORH               /* cursor_height */,
	CURSORPAD             /* cursor padding */,
	NULL                  /* init_plane_for_cursor */,
	set_cursor_image      /* set cursor image */,
	0                     /* vblank_query_supported */,
	create_custom_gem     /* create_custom_gem */,
};

struct drmmode_interface *drmmode_interface_get_implementation(int drm_fd)
{
	return &pl111_interface;
}
