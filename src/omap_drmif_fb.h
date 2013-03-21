/*
 * Copyright (C) 2012 ARM Limited
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
 */

#ifndef OMAP_DRMIF_FB_H_
#define OMAP_DRMIF_FB_H_

#include <stdint.h>

#define OMAP_PARAM_CHIPSET_ID 0

#define HW_CURSOR_ARGB		(0)
#define HW_CURSOR_PL111		(1)

/* Platform specific values for the flags element of the drm_mode_create_dumb struct
 * used by DRM_IOCTL_MODE_CREATE_DUMB. These are used by omap_bo_new_with_dim() to
 * request either a scanoutable or non-scanoutable buffer
 */
/*
 * THESE VALUES SHOULD BE CUSTOMISED FOR THE TARGET DRM DRIVER - SEE THE README FILE
 */
#define DRM_BO_SCANOUT				0x00000001			/* request scanout compatible buffer */
#define DRM_BO_NON_SCANOUT			0x00000000			/* request non-scanout compatible buffer */

/*
 * HARDWARE CURSOR SUPORT CONFIGURATION
 */

#define DRM_CURSOR_PLANE_FORMAT	HW_CURSOR_PL111
/**/


struct omap_bo;
struct omap_device;

enum omap_gem_op {
	OMAP_GEM_READ = 0x01,
	OMAP_GEM_WRITE = 0x02,
};

enum omap_buf_type {
	OMAP_BO_SCANOUT,
	OMAP_BO_NON_SCANOUT
};

struct omap_device *omap_device_new(int fd);
void omap_device_del(struct omap_device *dev);
int omap_bo_get_name(struct omap_bo *bo, uint32_t *name);
uint32_t omap_bo_handle(struct omap_bo *bo);
void *omap_bo_map(struct omap_bo *bo);
int omap_get_param(struct omap_device *dev, uint64_t param, uint64_t *value);
int omap_bo_add_fb(struct omap_bo *bo);
uint32_t omap_bo_get_fb(struct omap_bo *bo);
int omap_bo_cpu_prep(struct omap_bo *bo, enum omap_gem_op op);
int omap_bo_cpu_fini(struct omap_bo *bo, enum omap_gem_op op);
uint32_t omap_bo_size(struct omap_bo *bo);

struct omap_bo *omap_bo_new_with_dim(struct omap_device *dev, uint32_t width,
			uint32_t height, uint8_t depth, uint8_t bpp,
			enum omap_buf_type buf_type);
uint32_t omap_bo_width(struct omap_bo *bo);
uint32_t omap_bo_height(struct omap_bo *bo);
uint32_t omap_bo_bpp(struct omap_bo *bo);
uint32_t omap_bo_Bpp(struct omap_bo *bo);
uint32_t omap_bo_pitch(struct omap_bo *bo);

void omap_bo_reference(struct omap_bo *bo);
void omap_bo_unreference(struct omap_bo *bo);

/* When dmabuf is set on a bo, omap_bo_cpu_prep() waits for KDS shared access */
int omap_bo_set_dmabuf(struct omap_bo *bo);
void omap_bo_clear_dmabuf(struct omap_bo *bo);
int omap_bo_has_dmabuf(struct omap_bo *bo);
int omap_bo_clear(struct omap_bo *bo);

#endif /* OMAP_DRMIF_FB_H_ */

