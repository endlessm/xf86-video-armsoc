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
 *    Ian Elliott <ianelliottus@yahoo.com>
 *    Rob Clark <rob@ti.com>
 */

#ifndef __OMAP_UTIL_H__
#define __OMAP_UTIL_H__

/* All drivers need the following headers: */
#include "xf86.h"
#include "xf86_OSproc.h"

/**
 * This controls whether debug statements (and function "trace" enter/exit)
 * messages are sent to the log file (TRUE) or are ignored (FALSE).
 */
extern _X_EXPORT Bool omapDebug;


/* Various logging/debug macros for use in the X driver and the external
 * sub-modules:
 */
#define TRACE_ENTER() \
		do { if (omapDebug) xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s:%d: Entering\n",\
				__FUNCTION__, __LINE__); } while (0)
#define TRACE_EXIT() \
		do { if (omapDebug) xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s:%d: Exiting\n",\
				__FUNCTION__, __LINE__); } while (0)
#define DEBUG_MSG(fmt, ...) \
		do { if (omapDebug) xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s:%d " fmt "\n",\
				__FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)
#define INFO_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, X_INFO, fmt "\n",\
				##__VA_ARGS__); } while (0)
#define CONFIG_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, fmt "\n",\
				##__VA_ARGS__); } while (0)
#define WARNING_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "WARNING: " fmt "\n",\
				##__VA_ARGS__); } while (0)
#define ERROR_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "ERROR: " fmt "\n",\
				##__VA_ARGS__); } while (0)
#define EARLY_ERROR_MSG(fmt, ...) \
		do { xf86Msg(X_ERROR, "ERROR: " fmt "\n",\
				##__VA_ARGS__); } while (0)


extern unsigned int
OMAPCalculateStride(unsigned int fbWidth, unsigned int bitsPerPixel);
extern unsigned int
OMAPCalculateTiledStride(unsigned int width, unsigned int bitsPerPixel);

#endif /* __OMAP_UTIL_H__ */
