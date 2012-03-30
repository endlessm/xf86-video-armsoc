#include <xf86drm.h>
#include <xf86drmMode.h>
#include <stdlib.h>
#include "omap_drmif.h"

struct omap_device {
	int fd;
};

struct omap_bo {
	struct omap_device *dev;
	uint32_t handle;
	uint32_t size;
	int from_name;
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
	create_dumb.height = size / 16;
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

	return new_buf;
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
