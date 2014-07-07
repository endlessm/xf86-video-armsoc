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

#ifndef ARMSOC_DUMB_H_
#define ARMSOC_DUMB_H_

#include <stdint.h>

struct armsoc_bo;
struct armsoc_device;

enum armsoc_gem_op {
	ARMSOC_GEM_READ = 0x01,
	ARMSOC_GEM_WRITE = 0x02,
	ARMSOC_GEM_READ_WRITE = 0x03,
};

enum armsoc_buf_type {
	ARMSOC_BO_SCANOUT,
	ARMSOC_BO_NON_SCANOUT
};

/*
 * Generic GEM object information used to abstract custom GEM creation
 * for every DRM driver.
 */
struct armsoc_create_gem {
	/* parameters that will be provided  */
	uint32_t height;
	uint32_t width;
	uint32_t bpp;
	enum armsoc_buf_type buf_type;
	/* handle, pitch, size will be returned */
	uint32_t handle;
	uint32_t pitch;
	uint64_t size;
};

struct armsoc_device *armsoc_device_new(int fd,
	int (*create_custom_gem)(int fd, struct armsoc_create_gem *create_gem));
void armsoc_device_del(struct armsoc_device *dev);
int armsoc_bo_get_name(struct armsoc_bo *bo, uint32_t *name);
uint32_t armsoc_bo_handle(struct armsoc_bo *bo);
void *armsoc_bo_map(struct armsoc_bo *bo);
int armsoc_get_param(struct armsoc_device *dev, uint64_t param,
			uint64_t *value);
int armsoc_bo_add_fb(struct armsoc_bo *bo);
uint32_t armsoc_bo_get_fb(struct armsoc_bo *bo);
int armsoc_bo_cpu_prep(struct armsoc_bo *bo, enum armsoc_gem_op op);
int armsoc_bo_cpu_fini(struct armsoc_bo *bo, enum armsoc_gem_op op);
uint32_t armsoc_bo_size(struct armsoc_bo *bo);

struct armsoc_bo *armsoc_bo_new_with_dim(struct armsoc_device *dev,
			uint32_t width,
			uint32_t height, uint8_t depth, uint8_t bpp,
			enum armsoc_buf_type buf_type);
uint32_t armsoc_bo_width(struct armsoc_bo *bo);
uint32_t armsoc_bo_height(struct armsoc_bo *bo);
uint8_t armsoc_bo_depth(struct armsoc_bo *bo);
uint32_t armsoc_bo_bpp(struct armsoc_bo *bo);
uint32_t armsoc_bo_pitch(struct armsoc_bo *bo);

void armsoc_bo_reference(struct armsoc_bo *bo);
void armsoc_bo_unreference(struct armsoc_bo *bo);

/* When dmabuf is set on a bo, armsoc_bo_cpu_prep()
 *  waits for KDS shared access
 */
int armsoc_bo_set_dmabuf(struct armsoc_bo *bo);
void armsoc_bo_clear_dmabuf(struct armsoc_bo *bo);
int armsoc_bo_has_dmabuf(struct armsoc_bo *bo);
int armsoc_bo_clear(struct armsoc_bo *bo);
int armsoc_bo_rm_fb(struct armsoc_bo *bo);
int armsoc_bo_resize(struct armsoc_bo *bo, uint32_t new_width,
						uint32_t new_height);


#endif /* ARMSOC_DUMB_H_ */

