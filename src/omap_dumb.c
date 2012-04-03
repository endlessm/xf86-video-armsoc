#include <stdlib.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "omap_drmif.h"

struct omap_device {
	int fd;
};

struct omap_bo {
	struct omap_device *dev;
	uint32_t handle;
	uint32_t size;
	int from_name;
	void *map_addr;
};

/* device related functions:
 */

struct omap_device *omap_device_new(int fd)
{
	struct omap_device *new_dev = malloc(sizeof(*new_dev));
	if (!new_dev)
		return NULL;

	new_dev->fd = fd;
	return new_dev;
}

void omap_device_del(struct omap_device *dev)
{
	free(dev);
}

/* buffer-object related functions:
 */

struct omap_bo *omap_bo_new(struct omap_device *dev, uint32_t size, uint32_t flags)
{
	struct drm_mode_create_dumb create_dumb;
	struct omap_bo *new_buf;
	int res;

	new_buf = malloc(sizeof(*new_buf));
	if (!new_buf)
		return NULL;

	/* The dumb interface requires width, height and bpp but we only have the final size, so fake those to make them
	 * add up to size. This is OK as we get the real dimensions in drmAddFB.
	 */
	create_dumb.height = size / 2;
	create_dumb.width = 1;
	create_dumb.bpp = 16;
	create_dumb.flags = flags;

	res = drmIoctl(dev->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb);
	if (res)
	{
		free(new_buf);
		return NULL;
	}

	new_buf->dev = dev;
	new_buf->handle = create_dumb.handle;
	new_buf->size = create_dumb.size;
	new_buf->from_name = 0;
	new_buf->map_addr = NULL;

	return new_buf;
}

struct omap_bo *omap_bo_from_name(struct omap_device *dev, uint32_t name)
{
	struct omap_bo *new_buf;
	struct drm_gem_open gem_open;
	int res;

	new_buf = malloc(sizeof(*new_buf));
	if (!new_buf)
		return NULL;

	gem_open.name = name;

	res = drmIoctl(dev->fd, DRM_IOCTL_GEM_OPEN, &gem_open);
	if (res)
	{
		free(new_buf);
		return NULL;
	}

	new_buf->dev = dev;
	new_buf->handle = gem_open.handle;
	new_buf->size = gem_open.size;
	new_buf->from_name = 1;
	new_buf->map_addr = NULL;

	return new_buf;
}

void omap_bo_del(struct omap_bo *bo)
{
	if (!bo)
		return;

	if (bo->map_addr)
	{
		munmap(bo->map_addr, bo->size);
	}

	if (bo->from_name)
	{
		struct drm_gem_close gem_close;

		gem_close.handle = bo->handle;

		drmIoctl(bo->dev->fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
	}
	else
	{
		struct drm_mode_destroy_dumb destroy_dumb;

		destroy_dumb.handle = bo->handle;

		drmIoctl(bo->dev->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
	}
	free(bo);
}

int omap_bo_get_name(struct omap_bo *bo, uint32_t *name)
{
	int ret;
	struct drm_gem_flink flink;

	flink.handle = bo->handle;

	ret = drmIoctl(bo->dev->fd, DRM_IOCTL_GEM_FLINK, &flink);
	if (ret)
		return ret;

	*name = flink.name;
	return 0;
}

uint32_t omap_bo_handle(struct omap_bo *bo)
{
	return bo->handle;
}

uint32_t omap_bo_size(struct omap_bo *bo)
{
	return bo->size;
}

void *omap_bo_map(struct omap_bo *bo)
{
	if (!bo->map_addr)
	{
		struct drm_mode_map_dumb map_dumb;
		int res;

		map_dumb.handle = bo->handle;

		res = drmIoctl(bo->dev->fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb);
		if (res)
			return NULL;

		bo->map_addr = mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, bo->dev->fd, map_dumb.offset);
	}

	return bo->map_addr;
}

int omap_bo_cpu_prep(struct omap_bo *bo, enum omap_gem_op op)
{
	return 0;
}

int omap_bo_cpu_fini(struct omap_bo *bo, enum omap_gem_op op)
{
	return msync(bo->map_addr, bo->size, MS_SYNC | MS_INVALIDATE);
}

int omap_get_param(struct omap_device *dev, uint64_t param, uint64_t *value)
{
	*value = 0x0600;
	return 0;
}
