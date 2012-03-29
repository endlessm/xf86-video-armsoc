#include <xf86drm.h>
#include <stdlib.h>
#include "omap_drmif.h"

struct omap_device {
	int fd;
};

struct omap_bo {
	struct omap_device *dev;
	uint32_t handle;
};

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

int omap_bo_get_name(struct omap_bo *bo, uint32_t *name)
{
	int fd = bo->dev->fd;
	int ret;
	struct drm_gem_flink flink;

	flink.handle = bo->handle;

	ret = drmIoctl(fd, DRM_IOCTL_GEM_FLINK, &flink);
	if (ret)
		return ret;

	*name = flink.name;
	return 0;
}
