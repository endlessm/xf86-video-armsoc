/*
 * Copyright © 2013 ARM Limited.
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* TODO: MIDEGL-1430: cleanup #includes, remove unnecessary ones */

#include "xorg-server.h"
#include "xorgVersion.h"

#include <sys/stat.h>

#include <string.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

/* All drivers should typically include these */
#include "xf86.h"
#include "xf86_OSproc.h"
#define PPC_MMIO_IS_BE
#include "compiler.h"
#include "mipointer.h"

/* All drivers implementing backing store need this */
#include "mibstore.h"

#include "micmap.h"

#include "xf86DDC.h"

#include "xf86RandR12.h"
#include "dixstruct.h"
#include "scrnintstr.h"
#include "fb.h"
#include "xf86cmap.h"
#include "shadowfb.h"

#include "xf86xv.h"
#include <X11/extensions/Xv.h>

#include "xf86Cursor.h"
#include "xf86DDC.h"

#include "region.h"

#include <X11/extensions/randr.h>

#ifdef HAVE_XEXTPROTO_71
#include <X11/extensions/dpmsconst.h>
#else
#define DPMS_SERVER
#include <X11/extensions/dpms.h>
#endif

#include "omap_driver.h"

#include "xf86Crtc.h"

#include "xf86drmMode.h"

/* #define ARGB_LBBP_CONVERSION_DEBUG un-comment to enable cursor format conversion debugging. */

#if ( DRM_CURSOR_PLANE_FORMAT == HW_CURSOR_PL111 )

#define LBBP_BACKGROUND  (0x0)
#define LBBP_FOREGROUND  (0x1)
#define LBBP_TRANSPARENT (0x2)
#define LBBP_INVERSE     (0x3)

#define ARGB_ALPHA (0xff000000)
#define ARGB_RGB (~ARGB_ALPHA)


#define LBBP_WORDS_PER_LINE        (4)
#define LBBP_PIXELS_PER_WORD       (16)

/* shift required to locate pixel into the correct position in a cursor LBBP word,
 *  indexed by x mod 16.*/
const unsigned char x_mod_16_to_value_shift[LBBP_PIXELS_PER_WORD] = {
		6,4,2,0,14,12,10,8,22,20,18,16,30,28,26,24
};

/* Max index for x_mod_16_to_byte_shift array */
#define MAX_VALUE_SHIFT_INDEX  (LBBP_PIXELS_PER_WORD-1)

/* Pack the pixel value into its correct position in the buffer as specified
 * for LBBP */

static inline void
set_lbbp_pixel(uint32_t * buffer, unsigned int x, unsigned int y, uint32_t value )
{
	uint32_t * p;
	uint32_t shift;

	assert( (x < CURSORW) && (y < CURSORH) );

	shift = x_mod_16_to_value_shift[x % LBBP_PIXELS_PER_WORD];

	/* Get the word containing this pixel */
	p = buffer + (x >> LBBP_WORDS_PER_LINE) + (y << 2);

	/* Clear data for this pixel in the buffer and apply the new data */
	*p &= ~(LBBP_INVERSE << shift);
	*p |= value << shift;
}

void
drmmode_argb_cursor_to_pl111_lbbp( xf86CrtcPtr crtc, uint32_t * d, CARD32 *s, uint32_t size)
{
#ifdef ARGB_LBBP_CONVERSION_DEBUG
	/* Add 1 on width to insert trailing NULL */
	char string_cursor[CURSORW+1];
#endif /* ARGB_LBBP_CONVERSION_DEBUG */
	unsigned int x;
	unsigned int y;

	/* Convert the ARGB into PL111 LBBP */
	/* Ideally we would want to receive the image in this format from X, but for now just convert.
	 */
	for ( y = 0; y < CURSORH ; y++) {
		for ( x = 0; x < CURSORW ; x++) {
			uint32_t value = LBBP_TRANSPARENT;
			/* If pixel visible foreground/background */
			if ( ( *s & ARGB_ALPHA ) != 0 ) {
				/* Any color set then just convert to foreground for now */
				if ( ( *s & ARGB_RGB ) != 0 )
					value = LBBP_FOREGROUND;
				else
					value = LBBP_BACKGROUND;
			}
#ifdef ARGB_LBBP_CONVERSION_DEBUG
			if ( value == LBBP_TRANSPARENT ) {
				string_cursor[x] = 'T';
			} else if ( value == LBBP_FOREGROUND ) {
				string_cursor[x] = 'F';
			} else if ( value == LBBP_INVERSE ) {
				string_cursor[x] = 'I';
			} else {
				string_cursor[x] = 'B';
			}
#endif /* ARGB_LBBP_CONVERSION_DEBUG */
			set_lbbp_pixel( d, x, y, value );
			++s;
		}
#ifdef ARGB_LBBP_CONVERSION_DEBUG
		string_cursor[CURSORW] = '\0';
		xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "%s\n", string_cursor );
#endif /* ARGB_LBBP_CONVERSION_DEBUG */
	}
}
#endif /* ( DRM_CURSOR_PLANE_FORMAT == HW_CURSOR_PL111 ) */

