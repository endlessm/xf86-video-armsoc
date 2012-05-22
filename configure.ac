#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.60)
# XXX bug URL should be https://bugs.freedesktop.org/enter_bug.cgi?product=xorg
# but this makes autoheader choke..
AC_INIT([xf86-video-omap],
        [0.0.1],
        [https://bugs.freedesktop.org/enter_bug.cgi],
        [xf86-video-omap])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR(.)

AM_INIT_AUTOMAKE([dist-bzip2])

AM_MAINTAINER_MODE

# Require xorg-macros: XORG_DEFAULT_OPTIONS
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.4 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.4)
XORG_DEFAULT_OPTIONS

# Checks for programs.
AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_PROG_CC

AC_CHECK_HEADERS([sys/ioctl.h])

AH_TOP([#include "xorg-server.h"])

AC_ARG_WITH(xorg-module-dir,
            AC_HELP_STRING([--with-xorg-module-dir=DIR],
                           [Default xorg module directory [[default=$libdir/xorg/modules]]]),
            [moduledir="$withval"],
            [moduledir="$libdir/xorg/modules"])

# Checks for extensions
XORG_DRIVER_CHECK_EXT(RANDR, randrproto)
XORG_DRIVER_CHECK_EXT(RENDER, renderproto)
XORG_DRIVER_CHECK_EXT(XV, videoproto)
XORG_DRIVER_CHECK_EXT(DPMSExtension, xextproto)

# Checks for pkg-config packages
PKG_CHECK_MODULES(XORG, [xorg-server >= 1.3] xproto fontsproto libdrm libdrm_omap xf86driproto $REQUIRED_MODULES)
sdkdir=$(pkg-config --variable=sdkdir xorg-server)

# Checks for header files.
AC_HEADER_STDC

save_CFLAGS="$CFLAGS"
CFLAGS="$XORG_CFLAGS $DRI_CFLAGS $DRM_CFLAGS"
CPPFLAGS="$XORG_CFLAGS $DRI_CFLAGS $DRM_CFLAGS"
AC_MSG_CHECKING([whether to include DRI support])
if test x$DRI != xno; then
        AC_CHECK_HEADER([${sdkdir}/dri.h],
                      [have_dri_h="yes"], [have_dri_h="no"])
        AC_CHECK_HEADER([${sdkdir}/dristruct.h],
                      [have_dristruct_h="yes"], [have_dristruct_h="no"])
fi
AC_MSG_CHECKING([whether to include DRI support])
if test x$DRI = xauto; then
        if test "$have_dri_h" = yes -a \
                "$have_dristruct_h" = yes; then
                DRI="yes"
        else
                DRI="no"
        fi
fi
AC_MSG_RESULT([$DRI])
CFLAGS="$save_CFLAGS $DEBUGFLAGS"

AM_CONDITIONAL(DRI, test x$DRI = xyes)
if test "$DRI" = yes; then
        PKG_CHECK_MODULES(DRI, [xf86driproto glproto])
        AC_DEFINE(XF86DRI,1,[Enable DRI driver support])
        AC_DEFINE(XF86DRI_DEVEL,1,[Enable developmental DRI driver support])
fi

AM_CONDITIONAL(VIDEO_DEBUG, test x$VIDEO_DEBUG = xyes)
if test "$VIDEO_DEBUG" = yes; then
	AC_DEFINE(VIDEO_DEBUG,1,[Enable debug support])
fi

AC_SUBST([DRI_CFLAGS])

DRIVER_NAME=omap
AC_SUBST([DRIVER_NAME])
AC_SUBST([moduledir])

AC_OUTPUT([
	Makefile
	src/Makefile
	man/Makefile
])
