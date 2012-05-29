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
#define OMAP_DRMIF_FB_ H_

#include "omap_drmif.h"

/* Override flags used */
#undef OMAP_BO_TILED_32
#undef OMAP_BO_TILED_16
#undef OMAP_BO_TILED_8
#undef OMAP_BO_WC
#undef OMAP_BO_SCANOUT
#undef OMAP_BO_TILED
#undef OMAP_PARAM_CHIPSET_ID

#define OMAP_BO_TILED_32 0
#define OMAP_BO_TILED_16 0
#define OMAP_BO_TILED_8 0
#define OMAP_BO_WC 0
#define OMAP_BO_SCANOUT 0
#define OMAP_BO_TILED 0
#define OMAP_PARAM_CHIPSET_ID 0

void omap_bo_set_fb(struct omap_bo *bo, uint32_t fb_id);
uint32_t omap_bo_get_fb(struct omap_bo *bo);

struct omap_bo *omap_bo_new_with_dim(struct omap_device *dev, uint32_t width,
			uint32_t height, uint32_t bpp, uint32_t flags);
uint32_t omap_bo_width(struct omap_bo *bo);
uint32_t omap_bo_height(struct omap_bo *bo);
uint32_t omap_bo_bpp(struct omap_bo *bo);
uint32_t omap_bo_pitch(struct omap_bo *bo);

#endif /* OMAP_DRMIF_FB_H_ */

