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

/* This file is a staging area for OMAP DRM ioctl wrapper stuff that should
 * eventually become part of libdrm.. for now, I'm still sorting the API out
 * so it remains here until things stabilize.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <sys/mman.h>

#include "omap_drm.h"

struct omap_device {
	int fd;
};

/* a GEM buffer object allocated from the DRM device */
struct omap_bo {
	struct omap_device	*dev;
	void		*map;		/* userspace mmap'ing (if there is one) */
	uint32_t	size;
	uint32_t	handle;
	uint64_t	offset;		/* offset to mmap() */
};

struct omap_device * omap_device_new(int fd)
{
	struct omap_device *dev = calloc(sizeof(*dev), 1);
	dev->fd = fd;
	return dev;
}

void omap_device_del(struct omap_device *dev)
{
	free(dev);
}

int omap_get_param(struct omap_device *dev, uint64_t param, uint64_t *value)
{
	struct drm_omap_param req = {
			.param = param,
	};
	int ret;

	ret = drmCommandWriteRead(dev->fd, DRM_OMAP_GET_PARAM, &req, sizeof(req));
	if (ret) {
		return ret;
	}

	*value = req.value;

	return 0;
}

int omap_set_param(struct omap_device *dev, uint64_t param, uint64_t value)
{
	struct drm_omap_param req = {
			.param = param,
			.value = value,
	};
	return drmCommandWrite(dev->fd, DRM_OMAP_GET_PARAM, &req, sizeof(req));
}

/* allocate a new (un-tiled) buffer object */
struct omap_bo * omap_bo_new(struct omap_device *dev,
		uint32_t size, uint32_t flags)
{
	struct omap_bo *bo = calloc(sizeof(*bo), 1);
	struct drm_omap_gem_new req = {
			.size = size,
			.flags = flags,
	};

	if (size == 0) {
		goto fail;
	}

	if (!bo) {
		goto fail;
	}

	bo->dev = dev;
	bo->size = size;

	if (drmCommandWriteRead(dev->fd, DRM_OMAP_GEM_NEW,
				  &req, sizeof(req))) {
		goto fail;
	}

	bo->handle = req.handle;
	bo->offset = req.offset;

	return bo;

fail:
	free(bo);
	return NULL;
}

/* destroy a buffer object */
void omap_bo_del(struct omap_bo *bo)
{
	if (!bo) {
		return;
	}

	if (bo->map) {
		munmap(bo->map, bo->size);
	}

	if (bo->handle) {
		struct drm_gem_close req = {
				.handle = bo->handle,
		};

		drmIoctl(bo->dev->fd, DRM_IOCTL_GEM_CLOSE, &req);
	}

	free(bo);
}

uint32_t omap_bo_handle(struct omap_bo *bo)
{
	return bo->handle;
}

uint32_t omap_bo_size(struct omap_bo *bo)
{
	return bo->size;
}

void * omap_bo_map(struct omap_bo *bo)
{
	if (!bo->map) {
		bo->map = mmap(0, bo->size, PROT_READ | PROT_WRITE,
				 MAP_SHARED, bo->dev->fd, bo->offset);
		if (bo->map == MAP_FAILED) {
			bo->map = NULL;
		}
	}
	return bo->map;
}
