xf86-video-armsoc
=================
### Open-source X.org graphics driver for ARM graphics

**DRM driver selection**

While most operations use only the standard DRM modesetting interfaces, certain operations
unavoidably rely on specific driver behaviour (including dumb buffer allocation flags and cursor
plane z-ordering). As such, the armsoc driver must be configured for a particular DRM driver.

The currently supported DRM drivers are:
- pl111
- exynos

To configure armsoc for one of these, pass the `--with-drmmode` option to `./configure`. For example:
```
$ ./configure --with-drmmode=pl111
```
For other drivers, you will need to implement this support yourself. A template implementation is
provided in `src/drmmode_template` which can be built by passing `--with-drmmode=template` to `./configure`.
The interface is defined and documented in `src/drmmode_driver.h`, and you should refer to this while
modifying the template to set up your DRM driver's abstraction appropriately.

You can also copy `src/drmmode_template` into `src/drmmode_<yourdrivername>` and build with:
```
$ ./configure --with-drmmode=<yourdrivername>
```

