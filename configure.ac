#                                               -*- Autoconf -*-
#
# Copyright 2013 ARM
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# on the rights to use, copy, modify, merge, publish, distribute, sub
# license, and/or sell copies of the Software, and to permit persons to whom
# the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
# ARM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.60)
AC_INIT([xf86-video-armsoc],
        [1.0.0],
        [],
        [xf86-video-armsoc])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR(.)
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([dist-bzip2])

AM_MAINTAINER_MODE

# Require xorg-macros: XORG_DEFAULT_OPTIONS
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.4 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.4)
XORG_DEFAULT_OPTIONS

# Initialize libtool
LT_INIT([disable-static])
# Checks for programs.
AC_PROG_CC

AC_CHECK_HEADERS([sys/ioctl.h])
AC_CHECK_HEADERS([stdint.h])

AH_TOP([#include "xorg-server.h"])

AC_ARG_WITH(xorg-module-dir,
            AS_HELP_STRING([--with-xorg-module-dir=DIR],
                           [Default xorg module directory [[default=$libdir/xorg/modules]]]),
            [moduledir="$withval"],
            [moduledir="$libdir/xorg/modules"])

AC_MSG_CHECKING([which DRM driver to use])
AC_ARG_WITH(drmmode,
            AS_HELP_STRING([--with-drmmode],
                           [Which DRM driver to use (see README)]),
            [drmmode=$withval],
            AC_MSG_FAILURE([You must specify which DRM driver to build for - see README]))
AC_MSG_RESULT([$drmmode])
AC_SUBST(drmmode)

# Checks for extensions
XORG_DRIVER_CHECK_EXT(RANDR, randrproto)
XORG_DRIVER_CHECK_EXT(RENDER, renderproto)
XORG_DRIVER_CHECK_EXT(DPMSExtension, xextproto)

# Checks for pkg-config packages
PKG_CHECK_MODULES(XORG,
                  [xorg-server >= 1.10]
                  xproto
                  fontsproto
                  libdrm
                  dri2proto
                  pixman-1
                  $REQUIRED_MODULES)

# Checks for header files.
AC_HEADER_STDC


DRIVER_NAME=armsoc
AC_SUBST([DRIVER_NAME])
AC_SUBST([moduledir])

AC_OUTPUT([
	Makefile
	src/Makefile
	man/Makefile
])
