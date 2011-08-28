/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright © 2011 Texas Instruments, Inc
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
 *    Rob Clark <rob@ti.com>
 */

#ifndef OMAP_DRM_H_
#define OMAP_DRM_H_

#include <xf86drm.h>
#include <stdint.h>

/* This file is a staging area for OMAP DRM ioctl wrapper stuff that should
 * eventually become part of libdrm.. for now, I'm still sorting the API out
 * so it remains here until things stabilize.
 */


/**************************/
/**************************/
/**************************/
/* these should be in kernel header.. */
#define OMAP_PARAM_CHIPSET_ID	1	/* ie. 0x3430, 0x4430, etc */

struct drm_omap_param {
	uint64_t param;			/* in */
	uint64_t value;			/* in (set_param), out (get_param) */
};

struct drm_omap_get_base {
	char plugin_name[64];		/* in */
	uint32_t ioctl_base;		/* out */
};

#define OMAP_BO_SCANOUT		0x00000001	/* scanout capable (phys contiguous) */
#define OMAP_BO_CACHE_MASK	0x00000006	/* cache type mask, see cache modes */
#define OMAP_BO_TILED_MASK	0x00000f00	/* tiled mapping mask, see tiled modes */

/* cache modes */
#define OMAP_BO_CACHED		0x00000000	/* default */
#define OMAP_BO_WC		0x00000002	/* write-combine */
#define OMAP_BO_UNCACHED	0x00000004	/* strongly-ordered (uncached) */

/* tiled modes */
#define OMAP_BO_TILED_8		0x00000100
#define OMAP_BO_TILED_16	0x00000200
#define OMAP_BO_TILED_32	0x00000300

struct drm_omap_gem_new {
	union {				/* in */
		uint32_t bytes;		/* (for non-tiled formats) */
		struct {
			uint16_t width;
			uint16_t height;
		} tiled;		/* (for tiled formats) */
	} size;
	uint32_t flags;			/* in */
	uint32_t handle;		/* out */
};

/* mask of operations: */
enum omap_gem_op {
	OMAP_GEM_READ = 0x01,
	OMAP_GEM_WRITE = 0x02,
};

struct drm_omap_gem_cpu_prep {
	uint32_t handle;		/* buffer handle (in) */
	uint32_t op;			/* mask of omap_gem_op (in) */
};

struct drm_omap_gem_cpu_fini {
	uint32_t handle;		/* buffer handle (in) */
	uint32_t op;			/* mask of omap_gem_op (in) */
	/* TODO maybe here we pass down info about what regions are touched
	 * by sw so we can be clever about cache ops?  For now a placeholder,
	 * set to zero and we just do full buffer flush..
	 */
	uint32_t nregions;
};

struct drm_omap_gem_info {
	uint32_t handle;		/* buffer handle (in) */
	uint32_t pad;
	uint64_t offset;		/* out */
};

#define DRM_OMAP_GET_PARAM		0x00
#define DRM_OMAP_SET_PARAM		0x01
#define DRM_OMAP_GET_BASE		0x02
#define DRM_OMAP_GEM_NEW		0x03
#define DRM_OMAP_GEM_CPU_PREP	0x04
#define DRM_OMAP_GEM_CPU_FINI	0x05
#define DRM_OMAP_GEM_INFO	0x06
#define DRM_OMAP_NUM_IOCTLS		0x07
/**************************/
/**************************/
/**************************/


struct omap_bo;
struct omap_device;

/* device related functions:
 */

struct omap_device * omap_device_new(int fd);
void omap_device_del(struct omap_device *dev);
int omap_get_param(struct omap_device *dev, uint64_t param, uint64_t *value);
int omap_set_param(struct omap_device *dev, uint64_t param, uint64_t value);

/* buffer-object related functions:
 */

struct omap_bo * omap_bo_new(struct omap_device *dev,
		uint32_t size, uint32_t flags);
struct omap_bo * omap_bo_from_name(struct omap_device *dev, uint32_t name);
void omap_bo_del(struct omap_bo *bo);
int omap_bo_get_name(struct omap_bo *bo, uint32_t *name);
uint32_t omap_bo_handle(struct omap_bo *bo);
uint32_t omap_bo_size(struct omap_bo *bo);
void * omap_bo_map(struct omap_bo *bo);
int omap_bo_cpu_prep(struct omap_bo *bo, enum omap_gem_op op);
int omap_bo_cpu_fini(struct omap_bo *bo, enum omap_gem_op op);

#endif /* OMAP_DRM_H_ */
