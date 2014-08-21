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

/*
 * Parameters for different buffer objects:
 * bit [0]: backing storage
 *	(0 -> DMA)
 *	(1 -> SHM)
 * bit [2:1]: kind of mapping
 *	(0x0 -> uncached)
 *	(0x1 -> write combine)
 *	(0x2 -> cached)
 */
#define PL111_BOT_MASK		(0x7)
#define PL111_BOT_SHM		(0x0 << 0)
#define PL111_BOT_DMA		(0x1 << 0)
#define PL111_BOT_UNCACHED	(0x0 << 1)
#define PL111_BOT_WC		(0x1 << 1)
#define PL111_BOT_CACHED	(0x2 << 1)

/* TODO MIDEGL-1718: this should be included
 * from libdrm headers when pl111 is mainline */
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
		create_pl111.flags = PL111_BOT_DMA | PL111_BOT_UNCACHED;
	else
		create_pl111.flags = PL111_BOT_SHM | PL111_BOT_UNCACHED;

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
	1                     /* use_page_flip_events */,
	1                     /* use_early_display */,
	CURSORW               /* cursor width */,
	CURSORH               /* cursor_height */,
	CURSORPAD             /* cursor padding */,
	HWCURSOR_API_STANDARD /* cursor_api */,
	NULL                  /* init_plane_for_cursor */,
	0                     /* vblank_query_supported */,
	create_custom_gem     /* create_custom_gem */,
};

struct drmmode_interface *drmmode_interface_get_implementation(int drm_fd)
{
	return &pl111_interface;
}
