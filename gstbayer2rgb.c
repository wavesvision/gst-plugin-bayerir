/* 
 * GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * March 2008
 * Logic enhanced by William Brack <wbrack@mmm.com.hk>
 *
 * 2025 WavesVision <charlesb@wavesvision.com>
 * Modified standard bayer2rgb to accept bayer IR
 *
 */

/**
 * SECTION:element-bayerir2rgb
 * @title: bayerir2rgb (deprecated)
 *
 * Decodes raw camera bayer-IR to RGB.
 */

/*
 * In order to guard against my advancing maturity, some extra detailed
 * information about the logic of the decode is included here.  Much of
 * this was inspired by a technical paper from siliconimaging.com, which
 * in turn was based upon an article from IEEE,
 * T. Sakamoto, C. Nakanishi and T. Hase,
 * “Software pixel interpolation for digital still cameras suitable for
 *  a 32-bit MCU,”
 * IEEE Trans. Consumer Electronics, vol. 44, no. 4, November 1998.
 *
 * The code assumes a Bayer matrix of the type produced by the fourcc
 * BA81 (v4l2 format SBGGR8) of width w and height h which looks like:
 *       0 1 2 3  w-2 w-1
 *
 *   0   B G B G ....B G
 *   1   G R G R ....G R
 *   2   B G B G ....B G
 *       ...............
 * h-2   B G B G ....B G
 * h-1   G R G R ....G R
 *
 * We expand this matrix, producing a separate {r, g, b} triple for each
 * of the individual elements.  The algorithm for doing this expansion is
 * as follows.
 *
 * We are designing for speed of transformation, at a slight expense of code.
 * First, we calculate the appropriate triples for the four corners, the
 * remainder of the top and bottom rows, and the left and right columns.
 * The reason for this is that those elements are transformed slightly
 * differently than all of the remainder of the matrix. Finally, we transform
 * all of the remainder.
 *
 * The transformation into the "appropriate triples" is based upon the
 * "nearest neighbor" principal, with some additional complexity for the
 * calculation of the "green" element, where an "adaptive" pairing is used.
 *
 * For purposes of documentation and identification, each element of the
 * original array can be put into one of four classes:
 *   R   A red element
 *   B   A blue element
 *   GR  A green element which is followed by a red one
 *   GB  A green element which is followed by a blue one
 */

// Definiitions for testing
//#define BAYERIR2RGB_ORC_DISABLE					// Disable ORC functions
//#define BAYERIR2RGB_RINGBUFFER_CLEAR				// Clear ring buffer on allocation

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "gstbayerelements.h"
#include "gstbayerfuncs.h"
#include "gstbayerorc.h"
#include "gstbayer2rgb.h"


#define GST_CAT_DEFAULT gst_bayerir2rgb_debug

#define GST_TYPE_BAYERIR2RGB			(gst_bayerir2rgb_get_type())
#define GST_BAYERIR2RGB(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BAYERIR2RGB,GstBayerIR2RGB))
#define GST_IS_BAYERIR2RGB(obj)			(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BAYERIR2RGB))
#define GST_BAYERIR2RGB_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_BAYERIR2RGB,GstBayerIR2RGBClass))
#define GST_IS_BAYERIR2RGB_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_BAYERIR2RGB))
#define GST_BAYERIR2RGB_GET_CLASS(obj)		(G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_BAYERIR2RGB,GstBayerIR2RGBClass))
typedef struct _GstBayerIR2RGB			GstBayerIR2RGB;
typedef struct _GstBayerIRRGBClass		GstBayerIR2RGBClass;

typedef void (*GstBayerIR2RGBProcessFunc)	(GstBayerIR2RGB *, guint8 *, guint);


/* Plugin properties */
enum {
	PROP_0,
	PROP_DEBAYER_MODE,
	PROP_PACKED,
};

/* Debayer modes */
enum {
	GST_DEBAYER_MODE_RGBIR_FAST,
	GST_DEBAYER_MODE_RGBIR_1P3A,
	GST_DEBAYER_MODE_RGBIR_3P1A,
	GST_DEBAYER_MODE_RGBIR_1PSA,
	GST_DEBAYER_MODE_IR_SCALED,
} GstDebayerMode;

#define GST_DEBAYER_MODE_DEFAULT	GST_DEBAYER_MODE_RGBIR_3P1A
#define GST_PACKED_DEFAULT		FALSE

#define GST_DEBAYER_MODE (gst_debayer_mode_get_type ())
static GType
gst_debayer_mode_get_type (void) {
	static GType debayer_mode_type = 0;
	static const GEnumValue mode_types[] = {
		{GST_DEBAYER_MODE_RGBIR_FAST, "Debayer RGB-IR: Fast, but crude", "rgbir-fast"},
		{GST_DEBAYER_MODE_RGBIR_1P3A, "Debayer RGB-IR: Fixed average between pixels", "rgbir-1p3a"},
		{GST_DEBAYER_MODE_RGBIR_3P1A, "Debayer RGB-IR: Expands value to cover 3 pixels, averages in between", "rgbir-3p1a"},
		{GST_DEBAYER_MODE_RGBIR_1PSA, "Debayer RGB-IR: Stepped average", "rgbir-1psa"},
		{GST_DEBAYER_MODE_IR_SCALED, "Debayer IR: Scales IR to fit sensor dimensions", "ir-scaled"},
		{0, NULL, NULL}
	};

	if (!debayer_mode_type) {
		debayer_mode_type = g_enum_register_static ("GstDebayerMode", mode_types);
	}

	return debayer_mode_type;
}

GType gst_bayerir2rgb_get_type (void);

#define gst_bayerir2rgb_parent_class parent_class
G_DEFINE_TYPE (GstBayerIR2RGB, gst_bayerir2rgb, GST_TYPE_BASE_TRANSFORM);
GST_ELEMENT_REGISTER_DEFINE (bayerir2rgb, "bayerir2rgb", GST_RANK_NONE, gst_bayerir2rgb_get_type ());



/* FixMe: Shouldn't need theses */
/********************************/
#define GST_BAYERIR2RGB_SRC_FRAME_SIZE			6469632
#define GST_BAYERIR2RGB_SRC_FRAME_BYTES_PER_LINE	3328
#define GST_BAYERIR2RGB_SRC_FRAME_PIXELS_PER_LINE	2662


/* Plugin transforms		*/
/********************************/
/*
 * Methodology of standard Bayer routine
 *
 * Pre-process line of source data in 'src' into two neighboring
 * lines of temporary data 'dest0' and 'dest1' as follows. Note
 * that first two pixels of both 'dest0' and 'dest1' are special,
 * and so are the last two pixels of both 'dest0' and 'dest1' .
 *
 * The gist of this transformation is this:
 * - Assume the source data contain this BG bayer pattern on input
 *   data line 0 (this is the same as src[0, 1, 2, 3, 4, 5 ...]):
 *   src 0 : B0     G0          B1          G1          B2          G2
 * - This gets transformed into two lines:
 *   line 0: B0 avg(B0, B1)     B1      avg(B1, B2)     B2      avg(B2, B3)...
 *   line 1: G0     G0      avg(G0, G1)     G1      avg(G1, G2)     G2...
 * - Notice  ^^     ^^      ^^^^^^^^^^^     ^^      ^^^^^^^^^^^
 *   These are interpolated BG pairs, one for each input bayer pixel.
 *
 * The immediatelly following call to this function operates on the next
 * input data line 1 as follows:
 * - Assume the source data contain this GR bayer pattern on line 1:
 *   src 0 : G0     R0          G1          R1          G2          R2
 * - This gets transformed into two lines:
 *   line 2: G0 avg(G0, G1)     G1      avg(G1, G2)     G2      avg(G2, G3)...
 *   line 3: R0     R0      avg(R0, R1)     R1      avg(R1, R2)     R2...
 * - Notice  ^^     ^^      ^^^^^^^^^^^     ^^      ^^^^^^^^^^^
 *   These are interpolated GR pairs, one for each input bayer pixel.
 *
 * First two pixels are special, two more pixels as an example of the rest:
 *       ||   0    |       1        ||       2        |       3        ::
 * ------||--------+----------------++----------------+----------------+:
 * DEST0 || src[0] | avg(src[0, 2]) ||     src[2]     | avg(src[2, 4]) ::
 * ------||--------+----------------++----------------+----------------+:
 * DEST1 || src[1] |     src[1]     || avg(src[1, 3]) |     src[3]     ::
 * ------||--------+----------------++----------------+----------------+:
 *
 * Inner block of pixels:
 * :       |         n          |        n+1       :
 * :-------+--------------------+------------------:
 * : DEST0 |       src[n]       | avg(src[n, n+2]) :
 * :-------+--------------------+------------------:
 * : DEST1 | avg(src[n-1, n+1]) |     src[n+1]     :
 * :-------+--------------------+------------------:
 *
 * Last two pixels:
 *           w is EVEN                          w is ODD
 * :       |    w-2   |    w-1   ||   :       |    w-2   |    w-1   ||
 * :-------+----------+----------||   :-------+----------+----------||
 * : DEST0 | src[w-2] | src[w-2] ||   : DEST0 | src[w-3] | src[w-1] ||
 * :-------+----------+----------||   :-------+----------+----------||
 * : DEST1 | src[w-3] | src[w-1] ||   : DEST1 | src[w-2] | src[w-2] ||
 * :-------+----------+----------||   :-------+----------+----------||
 */

//static void
//gst_bayerir2rgb8_split_and_upsample_horiz (guint8 * dest0, guint8 * dest1, const guint8 * src, GstBayerIR2RGB * bayerir2rgb) {
//	int n = bayerir2rgb->width;
//	int i;
//
//	dest0[0] = src[0];
//	dest1[0] = src[1];
//	dest0[1] = (src[0] + src[2] + 1) >> 1;
//	dest1[1] = src[1];
//
//#if defined(__i386__) || defined(__amd64__)
//	bayer_orc_horiz_upsample_unaligned (dest0 + 2, dest1 + 2, src + 1, (n - 4) >> 1);
//#else
//	bayer_orc_horiz_upsample (dest0 + 2, dest1 + 2, src + 2, (n - 4) >> 1);
//#endif
//
//	for (i = n - 2; i < n; i++) {
//		if ((i & 1) == 0) {
//			dest0[i] = src[i];
//			dest1[i] = src[i - 1];
//		} else {
//			dest0[i] = src[i - 1];
//			dest1[i] = src[i];
//		}
//	}
//}

//static void
//gst_bayerir2rgb16_split_and_upsample_horiz (guint16 * dest0, guint16 * dest1, const guint16 * src, GstBayerIR2RGB * bayerir2rgb) {
//	int swap = bayerir2rgb->bigendian;
//	int n = bayerir2rgb->width;
//	int i;
//
//	dest0[0] = gswab16 (src[0], swap);
//	dest1[0] = gswab16 (src[1], swap);
//	dest0[1] = (gswab16 (src[0], swap) + gswab16 (src[2], swap) + 1) >> 1;
//	dest1[1] = gswab16 (src[1], swap);
//
//	if (swap) {
//		bayer16_orc_horiz_upsample_be (dest0 + 2, dest1 + 2, src + 1, (n - 4) >> 1);
//	} else {
//		bayer16_orc_horiz_upsample_le (dest0 + 2, dest1 + 2, src + 1, (n - 4) >> 1);
//	}
//
//	for (i = n - 2; i < n; i++) {
//		if ((i & 1) == 0) {
//			dest0[i] = gswab16 (src[i], swap);
//			dest1[i] = gswab16 (src[i - 1], swap);
//		} else {
//			dest0[i] = gswab16 (src[i - 1], swap);
//			dest1[i] = gswab16 (src[i], swap);
//		}
//	}
//}

//static void
//gst_bayerir2rgb_split_and_upsample_horiz (guint8 * dest0, guint8 * dest1, const guint8 * src, GstBayerIR2RGB * bayerir2rgb) {
//	if (bayerir2rgb->src_bpp == 8) {
//		gst_bayerir2rgb8_split_and_upsample_horiz (dest0, dest1, src, bayerir2rgb);
//	} else {
//		gst_bayerir2rgb16_split_and_upsample_horiz ((guint16 *) dest0, (guint16 *) dest1, (const guint16 *) src, bayerir2rgb);
//	}
//}



static guint16
gswab16 (guint16 val, guint8 swap) {
	if (swap) {
		return GUINT16_FROM_BE (val);
	} else {
		return val;
	}
}

//// Extract data from BGRG stream as original bayer filter expects (so 2nd colour goes on the 4th line)
//// Dest 0: Blue		Dest 1: Green		Dest 3: Red
//static void
//gst_bayerir2rgb16_split_and_upsample_horiz_bgrg (guint16 * dest0, guint16 * dest1, guint16 * dest3, const guint16 * src, GstBayerIR2RGB * bayerir2rgb) {
//	int swap = bayerir2rgb->bigendian;
//	int line_width = bayerir2rgb->width;
//	int pxlgrp_start, pxlgrp_end = line_width - 4;
//	guint16 prev_blue, prev_green, prev_red;
//
//	// First pixel group
//	// Blue
//	prev_blue = dest0[0] = gswab16 (src[0], swap);
//	// Green
//	dest1[0] = dest1[1] = gswab16 (src[1], swap);
//	dest1[2] = (gswab16 (src[1], swap) + gswab16 (src[3], swap) + 1) >> 1;
//	prev_green = dest1[3] = gswab16 (src[3], swap);
//	// Red
//	prev_red = dest3[0] = dest3[1] = dest3[2] = gswab16 (src[2], swap);
//
//	// ORC can't handle this
//	for (pxlgrp_start = 4; pxlgrp_start <= pxlgrp_end; pxlgrp_start+=4) {
//		// Copy pixels
//		// Blue
//		dest0[pxlgrp_start-3] = dest0[pxlgrp_start-2] = dest0[pxlgrp_start-1] = (gswab16 (src[pxlgrp_start+0], swap) + prev_blue + 1) >> 1;
//		prev_blue = dest0[pxlgrp_start+0] = gswab16 (src[pxlgrp_start+0], swap);
//		// Green
//		dest1[pxlgrp_start+0] = (gswab16 (src[pxlgrp_start+1], swap) + prev_green + 1) >> 1;
//		prev_green = dest1[pxlgrp_start+1] = gswab16 (src[pxlgrp_start+1], swap);
//		dest1[pxlgrp_start+2] = (gswab16 (src[pxlgrp_start+3], swap) + prev_green + 1) >> 1;
//		prev_green = dest1[pxlgrp_start+3] = gswab16 (src[pxlgrp_start+3], swap);
//		// Red
//		dest3[pxlgrp_start-1] = dest3[pxlgrp_start+0] = dest3[pxlgrp_start+1] = (gswab16 (src[pxlgrp_start+2], swap) + prev_red + 1) >> 1;
//		prev_red = dest3[pxlgrp_start+2] = gswab16 (src[pxlgrp_start+2], swap);
//	}
//
//	// Last pixel group
//	// Blue
//	dest0[pxlgrp_end+1] = dest0[pxlgrp_end+2] = dest0[pxlgrp_end+3] = prev_blue;
//	// Red
//	dest3[pxlgrp_end+3] = prev_red;
//}

// Extract data from GRGB stream as original bayer filter expects (so 2nd colour goes on the 4th line)
// Line 0: Red		Line 1: Green		Line 3: Blue
static void
gst_bayerir2rgb16_split_and_upsample_horiz_grgb (guint16 * dest0, guint16 * dest1, guint16 * dest3, const guint16 * src, GstBayerIR2RGB * bayerir2rgb) {
	int swap = bayerir2rgb->bigendian;
	int line_width = bayerir2rgb->width;
	int pxlgrp_start, pxlgrp_end = line_width - 4;
	guint16 avg_blue, avg_red;
	guint16 prev_blue, prev_green, prev_red;

	// Check which debayer mode to use
	switch (bayerir2rgb->debayer_mode) {
	case GST_DEBAYER_MODE_RGBIR_1P3A:
		// First pixel group
		// Red
		prev_red = dest0[0] = dest0[1] = gswab16 (src[1], swap);
		// Green
		prev_green = dest1[0] = gswab16 (src[0], swap);
		dest1[1] = (gswab16 (src[2], swap) + prev_green + 1) >> 1;
		prev_green = dest1[2] = gswab16 (src[2], swap);
		// Blue
		prev_blue = dest3[0] = dest3[1] = dest3[2] = dest3[3] = gswab16 (src[3], swap);

		// ORC can't handle this
		for (pxlgrp_start = 4; pxlgrp_start <= pxlgrp_end; pxlgrp_start+=4) {
			// Copy pixels
			// Red
			dest0[pxlgrp_start-2] = dest0[pxlgrp_start-1] = dest0[pxlgrp_start+0] = (gswab16 (src[pxlgrp_start+1], swap) + prev_red + 1) >> 1;
			prev_red = dest0[pxlgrp_start+1] = gswab16 (src[pxlgrp_start+1], swap);
			// Green
			dest1[pxlgrp_start-1] = (gswab16 (src[pxlgrp_start+0], swap) + prev_green + 1) >> 1;
			prev_green = dest1[pxlgrp_start+0] = gswab16 (src[pxlgrp_start+0], swap);
			dest1[pxlgrp_start+1] = (gswab16 (src[pxlgrp_start+2], swap) + prev_green + 1) >> 1;
			prev_green = dest1[pxlgrp_start+2] = gswab16 (src[pxlgrp_start+2], swap);
			// Blue
			dest3[pxlgrp_start+0] = dest3[pxlgrp_start+1] = dest3[pxlgrp_start+2] = (gswab16 (src[pxlgrp_start+3], swap) + prev_blue + 1) >> 1;
			prev_blue = dest3[pxlgrp_start+3] = gswab16 (src[pxlgrp_start+3], swap);
		}

		// Last pixel group
		// Red
		dest0[pxlgrp_end+2] = dest0[pxlgrp_end+3] = prev_red;
		// Green
		dest1[pxlgrp_end+3] = prev_green;

		break;
	case GST_DEBAYER_MODE_RGBIR_3P1A:
		// First pixel group
		// Red
		dest0[0] = dest0[1] = prev_red = gswab16 (src[1], swap);
		// Green
		dest1[0] = prev_green = gswab16 (src[0], swap);
		dest1[1] = (gswab16 (src[2], swap) + prev_green + 1) >> 1;
		dest1[2] = prev_green = gswab16 (src[2], swap);
		// Blue
		dest3[0] = dest3[1] = dest3[2] = dest3[3] = prev_blue = gswab16 (src[3], swap);

		// ORC can't handle this
		for (pxlgrp_start = 4; pxlgrp_start <= pxlgrp_end; pxlgrp_start+=4) {
			// Copy pixels
			// Red
			dest0[pxlgrp_start-2] = prev_red;
			dest0[pxlgrp_start-1] = (gswab16 (src[pxlgrp_start+1], swap) + prev_red + 1) >> 1;
			dest0[pxlgrp_start+0] = dest0[pxlgrp_start+1] = prev_red = gswab16 (src[pxlgrp_start+1], swap);
			// Green
			dest1[pxlgrp_start-1] = (gswab16 (src[pxlgrp_start+0], swap) + prev_green + 1) >> 1;
			dest1[pxlgrp_start+0] = prev_green = gswab16 (src[pxlgrp_start+0], swap);
			dest1[pxlgrp_start+1] = (gswab16 (src[pxlgrp_start+2], swap) + prev_green + 1) >> 1;
			dest1[pxlgrp_start+2] = prev_green = gswab16 (src[pxlgrp_start+2], swap);
			// Blue
			dest3[pxlgrp_start+0] = prev_blue;
			dest3[pxlgrp_start+1] = (gswab16 (src[pxlgrp_start+3], swap) + prev_blue + 1) >> 1;
			dest3[pxlgrp_start+2] = dest3[pxlgrp_start+3] = prev_blue = gswab16 (src[pxlgrp_start+3], swap);
		}

		// Last pixel group
		// Red
		dest0[pxlgrp_end+2] = dest0[pxlgrp_end+3] = prev_red;
		// Green
		dest1[pxlgrp_end+3] = prev_green;

		break;
	case GST_DEBAYER_MODE_RGBIR_1PSA:
		// First pixel group
		// Red
		prev_red = dest0[0] = dest0[1] = gswab16 (src[1], swap);
		// Green
		prev_green = dest1[0] = gswab16 (src[0], swap);
		dest1[1] = (gswab16 (src[2], swap) + prev_green + 1) >> 1;
		prev_green = dest1[2] = gswab16 (src[2], swap);
		// Blue
		prev_blue = dest3[0] = dest3[1] = dest3[2] = dest3[3] = gswab16 (src[3], swap);

		// ORC can't handle this
		for (pxlgrp_start = 4; pxlgrp_start <= pxlgrp_end; pxlgrp_start+=4) {
			// Copy pixels
			// Red
			dest0[pxlgrp_start-1] = avg_red = (prev_red + gswab16 (src[pxlgrp_start+1], swap) + 1) >> 1;
			dest0[pxlgrp_start-2] = (prev_red + avg_red + 1) >> 1;
			dest0[pxlgrp_start+0] = (avg_red + gswab16 (src[pxlgrp_start+1], swap) + 1) >> 1;
			dest0[pxlgrp_start+1] = prev_red = gswab16 (src[pxlgrp_start+1], swap);
			// Green
			dest1[pxlgrp_start-1] = (gswab16 (src[pxlgrp_start+0], swap) + prev_green + 1) >> 1;
			prev_green = dest1[pxlgrp_start+0] = gswab16 (src[pxlgrp_start+0], swap);
			dest1[pxlgrp_start+1] = (gswab16 (src[pxlgrp_start+2], swap) + prev_green + 1) >> 1;
			prev_green = dest1[pxlgrp_start+2] = gswab16 (src[pxlgrp_start+2], swap);
			// Blue
			dest3[pxlgrp_start+1] = avg_blue = (prev_blue + gswab16 (src[pxlgrp_start+3], swap) + 1) >> 1;
			dest3[pxlgrp_start+0] = (prev_blue + avg_blue + 1) >> 1;
			dest3[pxlgrp_start+2] = (avg_blue + gswab16 (src[pxlgrp_start+3], swap) + 1) >> 1;
			prev_blue = dest3[pxlgrp_start+3] = gswab16 (src[pxlgrp_start+3], swap);
		}

		// Last pixel group
		// Red
		dest0[pxlgrp_end+2] = dest0[pxlgrp_end+3] = prev_red;
		// Green
		dest1[pxlgrp_end+3] = prev_green;

		break;
	default:
		break;
	}
}

// Interpolate a line from 2 sources wither side
static void gst_bayerir2rgb16_interpolate_horiz (guint16 * dest0, const guint16 * src0, const guint16 * src1, GstBayerIR2RGB * bayerir2rgb) {
	int line_width = bayerir2rgb->width;

#ifndef BAYERIR2RGB_ORC_DISABLE
	// ORC routine
	bayer16_orc_horiz_interpolate_le (dest0, src0, src1, line_width - 1);
#else
	// ORC equivalent
	for (int i = 0; i < line_width; i++) {
		// Average pixels
		dest0[i] = (src0[i] + src1[i] + 1) >> 1;
	}
#endif
}

// Extract data from GIr stream as original bayer filter expects
static void
gst_bayerir2rgb16_split_and_upsample_horiz_gir_green (guint16 * dest0, const guint16 * src, GstBayerIR2RGB * bayerir2rgb) {
	int swap = bayerir2rgb->bigendian;
	int line_width = bayerir2rgb->width;
	int pxlgrp_end = line_width - 2;
	guint16 prev_green;

#ifndef BAYERIR2RGB_ORC_DISABLE
	// ORC routine
	bayer16_orc_horiz_split_and_upsample_gir_le (dest0 + 0, src + 0, pxlgrp_end >> 1);
#else
	// ORC equivalent
	for (int pxlgrp_start = 0; pxlgrp_start < pxlgrp_end; pxlgrp_start+=2) {
		// Green
		dest0[pxlgrp_start+0] = prev_green = gswab16 (src[pxlgrp_start+0], swap);
		dest0[pxlgrp_start+1] = (gswab16 (src[pxlgrp_start+2], swap) + prev_green + 1) >> 1;
	}
#endif

	// Last pixel group
	// Green
	dest0[pxlgrp_end+0] = prev_green = gswab16 (src[pxlgrp_end+0], swap);
	dest0[pxlgrp_end+1] = prev_green;
}

// Extract data from IrG stream as original bayer filter expects
static void
gst_bayerir2rgb16_split_and_upsample_horiz_irg_green (guint16 * dest0, const guint16 * src, GstBayerIR2RGB * bayerir2rgb) {
	int swap = bayerir2rgb->bigendian;
	int line_width = bayerir2rgb->width;
	int pxlgrp_end = line_width - 2;
	guint16 prev_green;

	// First pixel group
	// Green
	dest0[0] = dest0[1] = prev_green = gswab16 (src[1], swap);

#ifndef BAYERIR2RGB_ORC_DISABLE
	// ORC routine
	bayer16_orc_horiz_split_and_upsample_irg_le (dest0 + 2, src + 2, pxlgrp_end >> 1);
#else
	// ORC equivalent
	for (int pxlgrp_start = 2; pxlgrp_start <= pxlgrp_end; pxlgrp_start+=2) {
		// Green
		dest0[pxlgrp_start+0] = (gswab16 (src[pxlgrp_start+1], swap) + prev_green + 1) >> 1;
		dest0[pxlgrp_start+1] = prev_green = gswab16 (src[pxlgrp_start+1], swap);
	}
#endif
}


typedef void (*post_process_func) (guint8 * d0, const guint8 * s0, const guint8 * s1,
		const guint8 * s2, const guint8 * s3, const guint8 * s4, const guint8 * s5,
		int n);

typedef void (*post_process_func16) (guint16 * d0, guint16 * d1, const guint8 * s0,
		const guint8 * s1, const guint8 * s2, const guint8 * s3, const guint8 * s4,
		const guint8 * s5, int n);

// Calculate the offset to the line in the ring buffer corresponding to the frame line number
#define LINE(t, x, b) ((t) + (((x) & 7) * ((b)->width * DIV_ROUND_UP((b)->src_bpp, 8))))

static void
gst_bayerir2rgb_process (GstBayerIR2RGB * bayerir2rgb, uint8_t * dest, int dest_stride, uint8_t * src) {
	const uint8_t src_bytes_per_pixel = DIV_ROUND_UP (bayerir2rgb->src_bpp, 8);
	const uint32_t src_stride = GST_ROUND_UP_4 (bayerir2rgb->width) * src_bytes_per_pixel;
	const int bayersrc16 = bayerir2rgb->src_bpp > 8;
	int src_line, src_line_next;
	guint8 *rngbuf_pre_proc, *rngbuf_ptr1, *rngbuf_ptr2;
	guint32 *buf_post_proc = NULL;
	post_process_func merge[2] = { NULL, NULL };
	post_process_func16 merge16[2] = { NULL, NULL };
	int rout_off, gout_off, bout_off;

	/*
	 * Handle emission of either RGBA64 or RGBA (32bpp) . The default is
	 * emission of RGBA64 in case the input bayer data are >8 bit, since
	 * there is no loss of precision that way.
	 *
	 * The emission of RGBA (32bpp) as done here is done by shifting the
	 * debayered data by the bpp-8 bits right, to fit into the 8 bits per
	 * channel output buffer. This retains precision during calculation,
	 * and the calculation is a bit more expensive in terms of CPU cycles
	 * and memory. An alternative approach would be to downgrade the input
	 * bayer data in gst_bayerir2rgb16_split_and_upsample_horiz() already,
	 * and then perform this second part of debayering as if those input
	 * data were 8bpp bayer data. This would increase speed, but decrease
	 * precision.
	 */
	const int bayerdst16 = (dest_stride / bayerir2rgb->width / 4) == 2;

	/* We exploit some symmetry in the functions here.  The base functions
	 * are all named for the BGGR arrangement.  For RGGB, we swap the
	 * red offset and blue offset in the output.  For GRBG, we swap the
	 * order of the merge functions.  For GBRG, do both. */
	rout_off = bayerir2rgb->rout_off;
	gout_off = bayerir2rgb->gout_off;
	bout_off = bayerir2rgb->bout_off;
//	if (bayerir2rgb->format == GST_BAYERIR_2_RGB_FORMAT_RGGB ||
//			bayerir2rgb->format == GST_BAYERIR_2_RGB_FORMAT_GBRG) {
//		rout_off = bayerir2rgb->bout_off;
//		bout_off = bayerir2rgb->rout_off;
//	}

	if (rout_off == 2 && gout_off == 1 && bout_off == 0) {
		merge[0] = bayer_orc_merge_bg_bgra;
		merge[1] = bayer_orc_merge_gr_bgra;
		merge16[0] = bayer16_orc_merge_bg_bgra;
		merge16[1] = bayer16_orc_merge_gr_bgra;
	} else if (rout_off == 3 && gout_off == 2 && bout_off == 1) {
		merge[0] = bayer_orc_merge_bg_abgr;
		merge[1] = bayer_orc_merge_gr_abgr;
		merge16[0] = bayer16_orc_merge_bg_abgr;
		merge16[1] = bayer16_orc_merge_gr_abgr;
	} else if (rout_off == 1 && gout_off == 2 && bout_off == 3) {
		merge[0] = bayer_orc_merge_bg_argb;
		merge[1] = bayer_orc_merge_gr_argb;
		merge16[0] = bayer16_orc_merge_bg_argb;
		merge16[1] = bayer16_orc_merge_gr_argb;
	} else if (rout_off == 0 && gout_off == 1 && bout_off == 2) {
		merge[0] = bayer_orc_merge_bg_rgba;
		merge[1] = bayer_orc_merge_gr_rgba;
		merge16[0] = bayer16_orc_merge_bg_rgba;
		merge16[1] = bayer16_orc_merge_gr_rgba;
	}
//	if (bayerir2rgb->format == GST_BAYERIR_2_RGB_FORMAT_GRBG ||
//			bayerir2rgb->format == GST_BAYERIR_2_RGB_FORMAT_GBRG) {
//		post_process_func tmp = merge[0];
//		merge[0] = merge[1];
//		merge[1] = tmp;
//		post_process_func16 tmp16 = merge16[0];
//		merge16[0] = merge16[1];
//		merge16[1] = tmp16;
//	}

	// Allocate ring buffer for split/upsampled data
	// 2 rows out for each row in
	// Holds 4 source rows in total (for a total of 8)
	rngbuf_pre_proc = g_malloc (DIV_ROUND_UP (bayerir2rgb->src_bpp, 8) * 2 * 4 * bayerir2rgb->width);

#ifdef BAYERIR2RGB_RINGBUFFER_CLEAR
	// Clear ring buffer (not usually needed)
	memset(rngbuf_pre_proc, 0, DIV_ROUND_UP (bayerir2rgb->src_bpp, 8) * 2 * 4 * bayerir2rgb->width);
#endif

	// Allocate buffer for post processing if necessary
	// Required to hold 2 lines of data
	if (bayersrc16 || bayerdst16)
		buf_post_proc = g_malloc (sizeof (*buf_post_proc) * 2 * bayerir2rgb->width);

	/* Pre-process source line 1 into bottom two lines 6 and 7 as PREVIOUS line */
	gst_bayerir2rgb16_split_and_upsample_horiz_irg_green (						/* src line 1 */
				(guint16 *) (LINE (rngbuf_pre_proc, (3 * 2) + 0, bayerir2rgb)),		/* rngbuf_pre_proc buffer line 6 (Green) */
				(guint16 *) (src + (1 * src_stride)), bayerir2rgb);

	/* Pre-process source line 0 into top three lines 0,1, & 3 as CURRENT line */
	gst_bayerir2rgb16_split_and_upsample_horiz_grgb (						/* src line 0 */
				(guint16 *) (LINE (rngbuf_pre_proc, (3 * 2) + 1, bayerir2rgb)),		/* rngbuf_pre_proc buffer line 7 (Red) */
				(guint16 *) (LINE (rngbuf_pre_proc, (0 * 2) + 1, bayerir2rgb)),		/* rngbuf_pre_proc buffer line 1 (Green) */
				(guint16 *) (LINE (rngbuf_pre_proc, (0 * 2) + 0, bayerir2rgb)),		/* rngbuf_pre_proc buffer line 0 (Blue) */
				(guint16 *) (src + (0 * src_stride)), bayerir2rgb);
	/* Duplicate red row */
	memcpy (
		LINE (rngbuf_pre_proc, (0 * 2) + 1, bayerir2rgb),
		LINE (rngbuf_pre_proc, (3 * 2) + 1, bayerir2rgb),
		bayerir2rgb->width * DIV_ROUND_UP(bayerir2rgb->src_bpp, 8));

	// Iterate over frame
	for (src_line = 0; src_line < bayerir2rgb->height; src_line++) {
		if (src_line < bayerir2rgb->height - 1) {
			/*
			 * Pre-process NEXT source line (src_line + 1) into two consecutive lines
			 * (2 * (src_line + 1) + 0) % 8
			 * and
			 * (2 * (src_line + 1) + 1) % 8
			 *
			 * This cycle here starts with src_line=0, and therefore
			 * - reads source line 1
			 * - writes tmp buffer lines 2,3
			 * The cycle continues with source line 2 and tmp buffer lines 4,5 etc.
			 */
			src_line_next = src_line + 1;
			// Alternate lines are handled differently
			if ((src_line_next & 0x01) == 0) {
				// Colours swap on alternate lines
				if ((src_line_next & 0x03) > 0) {
					// Red
					rngbuf_ptr2 = LINE (rngbuf_pre_proc, (src_line_next * 2) + 3, bayerir2rgb);	/* rngbuf_pre_proc buffer line 7/3 */
					// Blue
					rngbuf_ptr1 = LINE (rngbuf_pre_proc, (src_line_next * 2) + 0, bayerir2rgb);	/* rngbuf_pre_proc buffer line 4/0 */
				} else {
					// Red
					rngbuf_ptr1 = LINE (rngbuf_pre_proc, (src_line_next * 2) + 3, bayerir2rgb);	/* rngbuf_pre_proc buffer line 7/3 */
					// Blue
					rngbuf_ptr2 = LINE (rngbuf_pre_proc, (src_line_next * 2) + 0, bayerir2rgb);	/* rngbuf_pre_proc buffer line 4/0 */
				}

				gst_bayerir2rgb16_split_and_upsample_horiz_grgb (
							(guint16 *) rngbuf_ptr1,
							(guint16 *) (LINE (rngbuf_pre_proc, (src_line_next * 2) + 1, bayerir2rgb)),	/* rngbuf_pre_proc buffer line 5/1 */
							(guint16 *) rngbuf_ptr2,
							(guint16 *) (src + (src_line_next * src_stride)), bayerir2rgb);

			} else {
				gst_bayerir2rgb16_split_and_upsample_horiz_irg_green (
							(guint16 *) (LINE (rngbuf_pre_proc, (src_line_next * 2) + 0, bayerir2rgb)),	/* rngbuf_pre_proc buffer line 2/6 */
							(guint16 *) (src + (src_line_next * src_stride)), bayerir2rgb);
			}
		}

		/*
		 * Use the pre-processed tmp buffer lines and construct resulting frame.
		 * Assume src_line=0, the tmp buffer content looks as follows:
		 *   line 0: B0 avg(B0, B1)     B1      avg(B1, B2)     B2      avg(B2, B3)...
		 *   line 1: G0     G0      avg(G0, G1)     G1      avg(G1, G2)     G2...
		 *   line 2: G0 avg(G0, G1)     G1      avg(G1, G2)     G2      avg(G2, G3)...
		 *   line 3: R0     R0      avg(R0, R1)     R1      avg(R1, R2)     R2...
		 *   line 4: empty
		 *   line 5: empty
		 *   line 6: G0 avg(G0, G1)     G1      avg(G1, G2)     G2      avg(G2, G3)...
		 *   line 7: R0     R0      avg(R0, R1)     R1      avg(R1, R2)     R2...
		 * Line 0,1 and 6,7 were populated in pre-process step outside of this loop.
		 * Line 2,3 was populated just above this comment.
		 * The code is currently processing source line 0 (not tmp buffer line 0)
		 * and uses the following tmp buffer lines in the process as inputs to the
		 * merge function:
		 * - tmp buffer line -2 => tmp buffer line 6 => orc g0 values
		 * - tmp buffer line -1 => tmp buffer line 7 => orc r0 values
		 * - tmp buffer line +0 => tmp buffer line 0 => orc b1 values
		 * - tmp buffer line +1 => tmp buffer line 1 => orc g1 values
		 * - tmp buffer line +2 => tmp buffer line 2 => orc g2 values
		 * - tmp buffer line +3 => tmp buffer line 3 => orc r2 values
		 * With src_line=0, the merge function used is one of bayer_orc_merge_bg_*
		 *
		 * A good material regarding the ORC functions below is
		 * https://www.siliconimaging.com/RGB%20Bayer.htm
		 * chapter
		 * "Interpolating the green component"
		 *
		 * The bayer_orc_merge_bg_* performs BG interpolation.
		 *   # average R from PREVIOUS line and NEXT line
		 *   r = [ avg(r0[0], r2[0]) , avg(r0[1], r2[1]) ]
		 *   # average G from PREVIOUS line and NEXT line
		 *   g = [ avg(g0[0], g2[0]) , avg(g0[1], g2[1]) ]
		 *   # copy CURRENT line G into variable t
		 *   t = [ g1[0]             , g1[1]             ]
		 *   # average G from PREVIOUS, CURRENT, NEXT line
		 *   g = [ avg(g[0], g1[0])  , avg(g[1], g1[1])  ]
		 *   # reorder the content of g and t variables using bit operations
		 *   # (the "g first, t second" order is here because the B pixel we
		 *   #  are now processing does not have its own G value, it only has
		 *   #  G value extrapolated from surrouding G pixels. The following
		 *   #  G pixel has its own G value, so we use it as-is, without any
		 *   #  extrapolation)
		 *   g = [ g, 0 ]
		 *   t = [ 0, t ]
		 *   g = [ g, t ]
		 *   # generate resulting (e.g. BGRx) data
		 *   bg = [ b1[0] , g   , b1[1] , t   ]
		 *   ra = [ r[0]  , 255 , r[1]  , 255 ]
		 *   d  = [ b1[0] , g   , r[0]  , 255 , b1[1] , t , r[1] , 255  ]
		 *
		 * In the next cycle, src_line=1, line 4,5 would be populated with BG values
		 * calculated from source line 2, merge function would use tmp buffer
		 * inputs from lines 0,1,2,3,4,5 i.e. b0,g0,g1,r1,b2,g2 and the merge
		 * function would be bayer_orc_merge_gr_* .
		 */
//		if (bayersrc16) {
			merge16[src_line & 1] ((guint16 *) buf_post_proc,						/* temporary buffer BG */
					(guint16 *) (buf_post_proc + bayerir2rgb->width),				/* temporary buffer GR */
					LINE (rngbuf_pre_proc, src_line * 2 - 2, bayerir2rgb),				/* PREVIOUS: even: BG g0 , odd: GR b0 */
					LINE (rngbuf_pre_proc, src_line * 2 - 1, bayerir2rgb),				/* PREVIOUS: even: BG r0 , odd: GR g0 */
					LINE (rngbuf_pre_proc, src_line * 2 + 0, bayerir2rgb),				/* CURRENT: even: BG b1 , odd: GR g1 */
					LINE (rngbuf_pre_proc, src_line * 2 + 1, bayerir2rgb),				/* CURRENT: even: BG g1 , odd: GR r1 */
					LINE (rngbuf_pre_proc, src_line * 2 + 2, bayerir2rgb),				/* NEXT: even: BG g2 , odd: GR b2 */
					LINE (rngbuf_pre_proc, src_line * 2 + 3, bayerir2rgb),				/* NEXT: even: BG r2 , odd: GR g2 */
					bayerir2rgb->width >> 1);

			if (bayerdst16)
				bayer16to16_orc_reorder (dest + src_line * dest_stride,
						buf_post_proc, buf_post_proc + bayerir2rgb->width, bayerir2rgb->src_bpp, bayerir2rgb->width);
			else
				bayer16to8_orc_reorder (dest + src_line * dest_stride,
						buf_post_proc, buf_post_proc + bayerir2rgb->width, bayerir2rgb->src_bpp - 8,
						bayerir2rgb->width);
//		} else {
//			merge[src_line & 1] (bayerdst16 ? (guint8 *) buf_post_proc : (dest + src_line * dest_stride),	/* output line src_line */
//					LINE (rngbuf_pre_proc, src_line * 2 - 2, bayerir2rgb),				/* PREVIOUS: even: BG g0 , odd: GR b0 */
//					LINE (rngbuf_pre_proc, src_line * 2 - 1, bayerir2rgb),				/* PREVIOUS: even: BG r0 , odd: GR g0 */
//					LINE (rngbuf_pre_proc, src_line * 2 + 0, bayerir2rgb),				/* CURRENT: even: BG b1 , odd: GR g1 */
//					LINE (rngbuf_pre_proc, src_line * 2 + 1, bayerir2rgb),				/* CURRENT: even: BG g1 , odd: GR r1 */
//					LINE (rngbuf_pre_proc, src_line * 2 + 2, bayerir2rgb),				/* NEXT: even: BG g2 , odd: GR b2 */
//					LINE (rngbuf_pre_proc, src_line * 2 + 3, bayerir2rgb),				/* NEXT: even: BG r2 , odd: GR g2 */
//					bayerir2rgb->width >> 1);
//			if (bayerdst16)
//				bayer8to16_orc_reorder (dest + src_line * dest_stride, buf_post_proc, bayerir2rgb->width);
//		}
	}

	// Free allocated memory
	if (bayersrc16)
		g_free (buf_post_proc);
	g_free (rngbuf_pre_proc);
}

static void
gst_bayerir2rgb_process_fast (GstBayerIR2RGB * bayerir2rgb, uint8_t * dest, int dest_stride, uint8_t * src) {
	const uint8_t src_bytes_per_pixel = DIV_ROUND_UP (bayerir2rgb->src_bpp, 8);
	const uint8_t dst_bytes_per_pixel = DIV_ROUND_UP (bayerir2rgb->dst_bpp, 8);
	uint64_t pixel_data[2];
	uint64_t *dst_ptr64, pixel_buffer;
	uint32_t *dst_ptr32;
	uint16_t src_x, src_y, *pixel_data_ptr = (uint16_t *) &pixel_data;
	uint16_t *pixel_buffer64_ptr = (uint16_t *) &pixel_buffer;
	uint8_t *pixel_buffer32_ptr =  (uint8_t *) &pixel_buffer;
	uint8_t *dst_ptr8, *dst_y;
	uint8_t *src_ptr;

	/*
	 * Handle emission of either RGBA64 or RGBA (32bpp) . The default is
	 * emission of RGBA64 in case the input bayer data are >8 bit, since
	 * there is no loss of precision that way.
	 *
	 * The emission of RGBA (32bpp) as done here is done by shifting the
	 * debayered data by the bpp-8 bits right, to fit into the 8 bits per
	 * channel output buffer. This retains precision during calculation,
	 * and the calculation is a bit more expensive in terms of CPU cycles
	 * and memory. An alternative approach would be to downgrade the input
	 * bayer data in gst_bayerir2rgb16_split_and_upsample_horiz() already,
	 * and then perform this second part of debayering as if those input
	 * data were 8bpp bayer data. This would increase speed, but decrease
	 * precision.
	 */
	//const int bayerdst16 = (dest_stride / bayerir2rgb->width / 4) == 2;

	// For each group of 4 lines
	for (src_y = 0; src_y < bayerir2rgb->height; src_y+=2) {
		// Calculate destination y offset
		dst_y = dest + (src_y * dest_stride);
		// For each group of 4x4 blocks per line
		for (src_x = 0; src_x < bayerir2rgb->width; src_x+=4) {
			// Calculate destination pointer
			dst_ptr8 = dst_y + (src_x * dst_bytes_per_pixel);
			// Extract colour values
			// Check whether source is packed
			if (bayerir2rgb->packed) {
				// Unpack data
				pixel_data[0] = extract_4_words_from_10bit_packed(src +
									((src_y + 0) * bayerir2rgb->src_stride) + ((src_x / 4) * bayerir2rgb->src_4pxl_bytes));
				pixel_data[1] = extract_4_words_from_10bit_packed(src +
									((src_y + 1) * bayerir2rgb->src_stride) + ((src_x / 4) * bayerir2rgb->src_4pxl_bytes));
			} else {
				src_ptr = src + ((src_y + 0) * bayerir2rgb->src_stride) + (src_x * src_bytes_per_pixel);
				pixel_data[0] = *((uint64_t *)  src_ptr);
				pixel_data[1] = *((uint64_t *) (src_ptr + bayerir2rgb->src_stride));
			}
			if (dst_bytes_per_pixel == 8) {
				// Convert to 16 bit
				pixel_data[0] = pixel_data[0] << 6;
				pixel_data[1] = pixel_data[1] << 6;
				// Convert destination pointer
				dst_ptr64 = (uint64_t *) dst_ptr8;

				// 1st Line
				////////////////
				// First create one pixel (R/B only, as this will be copied
				// Determine colour pixel offsets
				if (src_y & 0x2) {
					pixel_buffer64_ptr[bayerir2rgb->rout_off] = pixel_data_ptr[3];
					pixel_buffer64_ptr[bayerir2rgb->bout_off] = pixel_data_ptr[1];
				} else {
					pixel_buffer64_ptr[bayerir2rgb->rout_off] = pixel_data_ptr[1];
					pixel_buffer64_ptr[bayerir2rgb->bout_off] = pixel_data_ptr[3];
				}

				// First 2 pixels
				pixel_buffer64_ptr[bayerir2rgb->gout_off] = pixel_data_ptr[0];
				dst_ptr64[0] = dst_ptr64[1] = pixel_buffer;
				// Second 2 pixels
				pixel_buffer64_ptr[bayerir2rgb->gout_off] = pixel_data_ptr[2];
				dst_ptr64[2] = dst_ptr64[3] = pixel_buffer;

				// 2nd Line
				////////////////
				// Calculate destination pointer
				dst_ptr8 = dst_ptr8 + dest_stride;
				// Convert destination pointer
				dst_ptr64 = (uint64_t *) dst_ptr8;

				// First 2 pixels
				pixel_buffer64_ptr[bayerir2rgb->gout_off] = pixel_data_ptr[5];
				dst_ptr64[0] = dst_ptr64[1] = pixel_buffer;
				// Second 2 pixels
				pixel_buffer64_ptr[bayerir2rgb->gout_off] = pixel_data_ptr[7];
				dst_ptr64[2] = dst_ptr64[3] = pixel_buffer;
			} else {
				// Convert to 8 bit
				pixel_data[0] = pixel_data[0] >> 2;
				pixel_data[1] = pixel_data[1] >> 2;
				// Convert destination pointer
				dst_ptr32 = (uint32_t *) dst_ptr8;

				// 1st Line
				////////////////
				// First create one pixel (R/B only, as this will be copied
				// Determine colour pixel offsets
				if (src_y & 0x2) {
					pixel_buffer32_ptr[bayerir2rgb->rout_off] = pixel_data_ptr[3];
					pixel_buffer32_ptr[bayerir2rgb->bout_off] = pixel_data_ptr[1];
				} else {
					pixel_buffer32_ptr[bayerir2rgb->rout_off] = pixel_data_ptr[1];
					pixel_buffer32_ptr[bayerir2rgb->bout_off] = pixel_data_ptr[3];
				}

				// First 2 pixels
				pixel_buffer32_ptr[bayerir2rgb->gout_off] = pixel_data_ptr[0];
				if (dst_bytes_per_pixel == 4) {
					dst_ptr32[0] = dst_ptr32[1] = (uint32_t) pixel_buffer;
				} else {
					memcpy(dst_ptr8 + 0, pixel_buffer32_ptr, 3);
					memcpy(dst_ptr8 + 3, pixel_buffer32_ptr, 3);
				}
				// Second 2 pixels
				pixel_buffer32_ptr[bayerir2rgb->gout_off] = pixel_data_ptr[2];
				if (dst_bytes_per_pixel == 4) {
					dst_ptr32[2] = dst_ptr32[3] = (uint32_t) pixel_buffer;
				} else {
					memcpy(dst_ptr8 + 6, pixel_buffer32_ptr, 3);
					memcpy(dst_ptr8 + 9, pixel_buffer32_ptr, 3);
				}

				// 2nd Line
				////////////////
				// Calculate destination pointer
				dst_ptr8 = dst_ptr8 + dest_stride;
				// Convert destination pointer
				dst_ptr32 = (uint32_t *) dst_ptr8;

				// First 2 pixels
				pixel_buffer32_ptr[bayerir2rgb->gout_off] = pixel_data_ptr[5];
				if (dst_bytes_per_pixel == 4) {
					dst_ptr32[0] = dst_ptr32[1] = (uint32_t) pixel_buffer;
				} else {
					memcpy(dst_ptr8 + 0, pixel_buffer32_ptr, 3);
					memcpy(dst_ptr8 + 3, pixel_buffer32_ptr, 3);
				}
				// Second 2 pixels
				pixel_buffer32_ptr[bayerir2rgb->gout_off] = pixel_data_ptr[7];
				if (dst_bytes_per_pixel == 4) {
					dst_ptr32[2] = dst_ptr32[3] = (uint32_t) pixel_buffer;
				} else {
					memcpy(dst_ptr8 + 6, pixel_buffer32_ptr, 3);
					memcpy(dst_ptr8 + 9, pixel_buffer32_ptr, 3);
				}
			}
		}
	}
}

[[maybe_unused]] static void
gst_bayerir2rgb_process_ir_scaled (GstBayerIR2RGB * bayerir2rgb, uint8_t * dest, int dest_stride, uint8_t * src) {
	const uint8_t src_bytes_per_pixel = DIV_ROUND_UP (bayerir2rgb->src_bpp, 8);
	const uint32_t src_stride = GST_ROUND_UP_4 (bayerir2rgb->width) * src_bytes_per_pixel;

//	const int bayersrc16 = bayerir2rgb->src_bpp > 8;

	int src_line, src_line_next;
	guint8 *rngbuf_pre_proc;

	/*
	 * Handle emission of either RGBA64 or RGBA (32bpp) . The default is
	 * emission of RGBA64 in case the input bayer data are >8 bit, since
	 * there is no loss of precision that way.
	 *
	 * The emission of RGBA (32bpp) as done here is done by shifting the
	 * debayered data by the bpp-8 bits right, to fit into the 8 bits per
	 * channel output buffer. This retains precision during calculation,
	 * and the calculation is a bit more expensive in terms of CPU cycles
	 * and memory. An alternative approach would be to downgrade the input
	 * bayer data in gst_bayerir2rgb16_split_and_upsample_horiz() already,
	 * and then perform this second part of debayering as if those input
	 * data were 8bpp bayer data. This would increase speed, but decrease
	 * precision.
	 */
	const int bayerdst16 = (dest_stride / bayerir2rgb->width / 4) == 2;

	// Allocate ring buffer for split/upsampled data
	// 1 row out for 1 row in
	// Holds 8 source rows in total (so as not to have to redefine LINE)
	rngbuf_pre_proc = g_malloc (DIV_ROUND_UP (bayerir2rgb->src_bpp, 8) * 8 * bayerir2rgb->width);

#ifdef BAYERIR2RGB_RINGBUFFER_CLEAR
	// Clear ring buffer (not usually needed)
	memset(rngbuf_pre_proc, 0, DIV_ROUND_UP (bayerir2rgb->src_bpp, 8) * 8 * bayerir2rgb->width);
#endif

	/* Pre-process source line 1 into line 7 in the ring buffer */
	gst_bayerir2rgb16_split_and_upsample_horiz_gir_green (								/* src line 1 */
				(guint16 *) (LINE (rngbuf_pre_proc, (3 * 2) + 1, bayerir2rgb)),				/* rngbuf_pre_proc buffer line 7 */
				(guint16 *) (src + (1 * src_stride)), bayerir2rgb);

	// Iterate over frame
	for (src_line = 0; src_line < bayerir2rgb->height; src_line+=2) {
		if (src_line < bayerir2rgb->height - 1) {
			/*
			 * Pre-process NEXT source line (src_line + 1) into the corresponding position in the ring buffer
			 */
			src_line_next = src_line + 1;
			/* Extract IR data (using alternate gir function works) */
			gst_bayerir2rgb16_split_and_upsample_horiz_gir_green (
					(guint16 *) (LINE (rngbuf_pre_proc, src_line_next, bayerir2rgb)),		/* rngbuf_pre_proc buffer line 1/3/5/7 */
					(guint16 *) (src + (src_line_next * src_stride)), bayerir2rgb);
			/* Interpolate line from existing data */
			gst_bayerir2rgb16_interpolate_horiz (
					(guint16 *) (LINE (rngbuf_pre_proc, src_line_next-1, bayerir2rgb)),
					(guint16 *) (LINE (rngbuf_pre_proc, src_line_next-2, bayerir2rgb)),
					(guint16 *) (LINE (rngbuf_pre_proc, src_line_next, bayerir2rgb)),
					bayerir2rgb);
		}

		/*
		 * Ring buffer should contain current line and next line to output
		 */
//		if (bayersrc16) {
			if (bayerdst16) {
				// Emitting greyscale so only alpha channel important
				if ((bayerir2rgb->rout_off == 0) || (bayerir2rgb->gout_off == 0) || (bayerir2rgb->bout_off == 0)) {
					// 16-bit: Copy data to output
					bayer16_orc_greyscale_2_rgba64 ((guint64 *) (dest + ((src_line+0) * dest_stride)),
									(guint16 *) (LINE (rngbuf_pre_proc, src_line + 0, bayerir2rgb)),
									bayerir2rgb->width);
					bayer16_orc_greyscale_2_rgba64 ((guint64 *) (dest + ((src_line+1) * dest_stride)),
									(guint16 *) (LINE (rngbuf_pre_proc, src_line + 1, bayerir2rgb)),
									bayerir2rgb->width);
				} else {
					// 16-bit: Copy data to output
					bayer16_orc_greyscale_2_argb64 ((guint64 *) (dest + ((src_line+0) * dest_stride)),
									(guint16 *) (LINE (rngbuf_pre_proc, src_line + 0, bayerir2rgb)),
									bayerir2rgb->width);
					bayer16_orc_greyscale_2_argb64 ((guint64 *) (dest + ((src_line+1) * dest_stride)),
									(guint16 *) (LINE (rngbuf_pre_proc, src_line + 1, bayerir2rgb)),
									bayerir2rgb->width);
				}
			} else {
				// Emitting greyscale so only alpha channel important
				if ((bayerir2rgb->rout_off == 0) || (bayerir2rgb->gout_off == 0) || (bayerir2rgb->bout_off == 0)) {
					// 8-bit: Copy data to output
					bayer16_orc_greyscale_2_rgba32 ((guint32 *) (dest + ((src_line+0) * dest_stride)),
									(guint16 *) (LINE (rngbuf_pre_proc, src_line + 0, bayerir2rgb)),
									bayerir2rgb->src_bpp - 8,
									bayerir2rgb->width);
					bayer16_orc_greyscale_2_rgba32 ((guint32 *) (dest + ((src_line+1) * dest_stride)),
									(guint16 *) (LINE (rngbuf_pre_proc, src_line + 1, bayerir2rgb)),
									bayerir2rgb->src_bpp - 8,
									bayerir2rgb->width);
				} else {
					// 8-bit: Copy data to output
					bayer16_orc_greyscale_2_argb32 ((guint32 *) (dest + ((src_line+0) * dest_stride)),
									(guint16 *) (LINE (rngbuf_pre_proc, src_line + 0, bayerir2rgb)),
									bayerir2rgb->src_bpp - 8,
									bayerir2rgb->width);
					bayer16_orc_greyscale_2_argb32 ((guint32 *) (dest + ((src_line+1) * dest_stride)),
									(guint16 *) (LINE (rngbuf_pre_proc, src_line + 1, bayerir2rgb)),
									bayerir2rgb->src_bpp - 8,
									bayerir2rgb->width);
				}
			}
//		} else {
//			merge[src_line & 1] (bayerdst16 ? (guint8 *) buf_post_proc : (dest + src_line * dest_stride),	/* output line src_line */
//					LINE (rngbuf_pre_proc, src_line * 2 - 2, bayerir2rgb),				/* PREVIOUS: even: BG g0 , odd: GR b0 */
//					LINE (rngbuf_pre_proc, src_line * 2 - 1, bayerir2rgb),				/* PREVIOUS: even: BG r0 , odd: GR g0 */
//					LINE (rngbuf_pre_proc, src_line * 2 + 0, bayerir2rgb),				/* CURRENT: even: BG b1 , odd: GR g1 */
//					LINE (rngbuf_pre_proc, src_line * 2 + 1, bayerir2rgb),				/* CURRENT: even: BG g1 , odd: GR r1 */
//					LINE (rngbuf_pre_proc, src_line * 2 + 2, bayerir2rgb),				/* NEXT: even: BG g2 , odd: GR b2 */
//					LINE (rngbuf_pre_proc, src_line * 2 + 3, bayerir2rgb),				/* NEXT: even: BG r2 , odd: GR g2 */
//					bayerir2rgb->width >> 1);
//			if (bayerdst16)
//				bayer8to16_orc_reorder (dest + src_line * dest_stride, buf_post_proc, bayerir2rgb->width);
//		}
	}

	// Free allocated memory
	g_free (rngbuf_pre_proc);
}

#define MODE_NA(p)	if (p) { GST_WARNING_OBJECT (base, "Mode not available..."); break; }

static GstFlowReturn
gst_bayerir2rgb_transform (GstBaseTransform * base, GstBuffer * inbuf, GstBuffer * outbuf) {
	GstBayerIR2RGB *filter = GST_BAYERIR2RGB (base);
	GstMapInfo map;
	uint8_t *output;
	GstVideoFrame frame;

	GST_DEBUG ("transforming buffer");

	if (!gst_buffer_map (inbuf, &map, GST_MAP_READ))
		goto map_failed;

	if (!gst_video_frame_map (&frame, &filter->info, outbuf, GST_MAP_WRITE)) {
		gst_buffer_unmap (inbuf, &map);
		goto map_failed;
	}

	output = GST_VIDEO_FRAME_PLANE_DATA (&frame, 0);
	/* Select debayer method to use */
	switch (filter->debayer_mode) {
		case GST_DEBAYER_MODE_RGBIR_FAST:
			gst_bayerir2rgb_process_fast (filter, output, frame.info.stride[0], map.data);
			break;
		case GST_DEBAYER_MODE_RGBIR_1P3A:
		case GST_DEBAYER_MODE_RGBIR_3P1A:
		case GST_DEBAYER_MODE_RGBIR_1PSA:
			MODE_NA(filter->packed)
			gst_bayerir2rgb_process (filter, output, frame.info.stride[0], map.data);
			break;
//		case GST_DEBAYER_MODE_IR_SCALED:
//			gst_bayerir2rgb_process_ir_scaled (filter, output, frame.info.stride[0], map.data);
//			break;
		default:
			GST_WARNING_OBJECT (filter, "Invalid debayer mode selected!");
			break;
	}

	gst_video_frame_unmap (&frame);
	gst_buffer_unmap (inbuf, &map);

	return GST_FLOW_OK;

map_failed:
	GST_WARNING_OBJECT (base, "Could not map buffer, skipping");
	return GST_FLOW_OK;
}

/* Plugin Methods		*/
/********************************/
static void
gst_bayerir2rgb_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec) {
	GstBayerIR2RGB *bayerir2rgb = GST_BAYERIR2RGB (object);

	switch (prop_id) {
		case PROP_PACKED:
			GST_OBJECT_LOCK (bayerir2rgb);
			bayerir2rgb->packed = g_value_get_boolean (value);
			GST_OBJECT_UNLOCK (bayerir2rgb);
			break;
		case PROP_DEBAYER_MODE:
			gint debayer_mode = g_value_get_enum (value);
			GST_OBJECT_LOCK (bayerir2rgb);
			bayerir2rgb->debayer_mode = debayer_mode;
			GST_OBJECT_UNLOCK (bayerir2rgb);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gst_bayerir2rgb_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec) {
	GstBayerIR2RGB *bayerir2rgb = GST_BAYERIR2RGB (object);

	switch (prop_id) {
		case PROP_PACKED:
			GST_OBJECT_LOCK (bayerir2rgb);
			g_value_set_boolean (value, bayerir2rgb->packed);
			GST_OBJECT_UNLOCK (bayerir2rgb);
			break;
		case PROP_DEBAYER_MODE:
			GST_OBJECT_LOCK (bayerir2rgb);
			g_value_set_enum (value, bayerir2rgb->debayer_mode);
			GST_OBJECT_UNLOCK (bayerir2rgb);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static gboolean
gst_bayerir2rgb_set_caps (GstBaseTransform * base, GstCaps * incaps, GstCaps * outcaps) {
	GstBayerIR2RGB *bayerir2rgb = GST_BAYERIR2RGB (base);
	GstStructure *structure;
	const char *format;
	GstVideoInfo info;

	GST_DEBUG ("in caps %" GST_PTR_FORMAT " out caps %" GST_PTR_FORMAT, incaps, outcaps);

	// Get sink configuration
	structure = gst_caps_get_structure (incaps, 0);

	gst_structure_get_int (structure, "width", &bayerir2rgb->width);
	gst_structure_get_int (structure, "height", &bayerir2rgb->height);

	format = gst_structure_get_string (structure, "format");
	if (g_str_has_prefix (format, "bggr")) {
		bayerir2rgb->format = GST_BAYERIR_2_RGB_FORMAT_BGGR;
	} else if (g_str_has_prefix (format, "gbrg")) {
		bayerir2rgb->format = GST_BAYERIR_2_RGB_FORMAT_GBRG;
	} else if (g_str_has_prefix (format, "grbg")) {
		bayerir2rgb->format = GST_BAYERIR_2_RGB_FORMAT_GRBG;
	} else if (g_str_has_prefix (format, "rggb")) {
		bayerir2rgb->format = GST_BAYERIR_2_RGB_FORMAT_RGGB;
	} else {
		return FALSE;
	}

	if (strlen (format) == 4) {							/* 8bit bayer */
		bayerir2rgb->src_bpp = 8;
	} else if (strlen (format) == 8) {						/* 10/12/14/16 le/be bayer */
		bayerir2rgb->src_bpp = (gint) g_ascii_strtoull (format + 4, NULL, 10);
		if (bayerir2rgb->src_bpp & 1)						/* odd bayerir2rgb->src_bpp bayer formats not supported */
			return FALSE;
		if (bayerir2rgb->src_bpp < 10 || bayerir2rgb->src_bpp > 16)			/* bayer 10,12,14,16 only */
			return FALSE;

		if (g_str_has_suffix (format, "le"))
			bayerir2rgb->bigendian = 0;
		else if (g_str_has_suffix (format, "be"))
			bayerir2rgb->bigendian = 1;
		else
			return FALSE;
	} else {
		return FALSE;
	}

	// Set source stride (line length in bytes)
	if (bayerir2rgb->packed) {
		// Only 10-bit packed is supported
		if (bayerir2rgb->src_bpp != 10) return FALSE;

		bayerir2rgb->src_4pxl_bytes = 5;
		bayerir2rgb->src_stride = GST_BAYERIR2RGB_SRC_FRAME_BYTES_PER_LINE;
	} else {
		bayerir2rgb->src_4pxl_bytes = 8;
		bayerir2rgb->src_stride = GST_ROUND_UP_4 (bayerir2rgb->width) * DIV_ROUND_UP (bayerir2rgb->src_bpp, 8);
	}

	// Get source configuration
	structure = gst_caps_get_structure (outcaps, 0);
	format = gst_structure_get_string (structure, "format");
	if (strlen (format) == 3) {
		bayerir2rgb->dst_bpp = 24;
	} else if (strlen (format) == 4) {
		bayerir2rgb->dst_bpp = 32;
	} else {
		bayerir2rgb->dst_bpp = 64;
	}

	/* To cater for different RGB output formats, we need to set params for later */
	gst_video_info_from_caps (&info, outcaps);
	bayerir2rgb->rout_off = GST_VIDEO_INFO_COMP_OFFSET (&info, 0) / DIV_ROUND_UP (GST_VIDEO_INFO_COMP_DEPTH (&info, 0), 8);
	bayerir2rgb->gout_off = GST_VIDEO_INFO_COMP_OFFSET (&info, 1) / DIV_ROUND_UP (GST_VIDEO_INFO_COMP_DEPTH (&info, 1), 8);
	bayerir2rgb->bout_off = GST_VIDEO_INFO_COMP_OFFSET (&info, 2) / DIV_ROUND_UP (GST_VIDEO_INFO_COMP_DEPTH (&info, 2), 8);
	bayerir2rgb->aout_off = GST_VIDEO_INFO_COMP_OFFSET (&info, 3) / DIV_ROUND_UP (GST_VIDEO_INFO_COMP_DEPTH (&info, 3), 8);

	GST_DEBUG ("Colour pixel offsets:   R:%u   G:%u   B:%u   A:%u", bayerir2rgb->rout_off,
									bayerir2rgb->gout_off,
									bayerir2rgb->bout_off,
									bayerir2rgb->aout_off);

	bayerir2rgb->info = info;

	return TRUE;
}

static void
gst_bayerir2rgb_reset (GstBayerIR2RGB * filter) {
	filter->width = 0;
	filter->height = 0;
	filter->rout_off = 0;
	filter->gout_off = 0;
	filter->bout_off = 0;
	filter->aout_off = 0;
	filter->src_bpp = 8;
	filter->src_4pxl_bytes = 0;
	filter->src_stride = 0;
	filter->dst_bpp = 0;
	filter->bigendian = 0;
	filter->debayer_mode = GST_DEBAYER_MODE_DEFAULT;
	filter->packed = GST_PACKED_DEFAULT;
	gst_video_info_init (&filter->info);
}

static GstCaps *
gst_bayerir2rgb_transform_caps (GstBaseTransform * base, GstPadDirection direction, GstCaps * caps, GstCaps * filter) {
	GstBayerIR2RGB *bayerir2rgb;
	GstCaps *res_caps, *tmp_caps;
	GstStructure *structure;
	guint i, caps_size;

	bayerir2rgb = GST_BAYERIR2RGB (base);

	res_caps = gst_caps_copy (caps);
	caps_size = gst_caps_get_size (res_caps);
	for (i = 0; i < caps_size; i++) {
		structure = gst_caps_get_structure (res_caps, i);
		if (direction == GST_PAD_SINK) {
			gst_structure_set_name (structure, "video/x-raw");
			gst_structure_remove_field (structure, "format");
		} else {
			gst_structure_set_name (structure, "video/x-bayer");
			gst_structure_remove_fields (structure, "format", "colorimetry", "chroma-site", NULL);
		}
	}
	if (filter) {
		tmp_caps = res_caps;
		res_caps = gst_caps_intersect_full (filter, tmp_caps, GST_CAPS_INTERSECT_FIRST);
		gst_caps_unref (tmp_caps);
	}
	GST_DEBUG_OBJECT (bayerir2rgb, "transformed %" GST_PTR_FORMAT " into %" GST_PTR_FORMAT, caps, res_caps);
	return res_caps;
}

static gboolean
gst_bayerir2rgb_get_unit_size (GstBaseTransform * base, GstCaps * caps, gsize * size) {
	GstStructure *structure;
	GstBayerIR2RGB *bayerir2rgb;
	int width;
	int height;
	const char *name;

	structure = gst_caps_get_structure (caps, 0);
	bayerir2rgb = GST_BAYERIR2RGB (base);

	if (gst_structure_get_int (structure, "width", &width) && gst_structure_get_int (structure, "height", &height)) {
		name = gst_structure_get_name (structure);
		/* Our name must be either video/x-bayer video/x-raw */
		if (strcmp (name, "video/x-raw") == 0) {
			/* For output, calculate according to format */
			*size = width * height * DIV_ROUND_UP (bayerir2rgb->dst_bpp, 8);
			return TRUE;
		} else {
			if (bayerir2rgb->packed) {
				/* FixMe: Need to get this from V4L2 stream */
				*size = GST_BAYERIR2RGB_SRC_FRAME_SIZE;
			} else {
				*size = GST_ROUND_UP_4 (width) * height * DIV_ROUND_UP (bayerir2rgb->src_bpp, 8);
			}
			return TRUE;
		}
	}
	GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Incomplete caps, some required field missing"));
	return FALSE;
}

/* Plugin init                  */
/********************************/
static void
gst_bayerir2rgb_init (GstBayerIR2RGB * filter) {
	gst_bayerir2rgb_reset (filter);
	gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), FALSE);
}

static void
gst_bayerir2rgb_class_init (GstBayerIR2RGBClass * klass) {
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	gobject_class->set_property = gst_bayerir2rgb_set_property;
	gobject_class->get_property = gst_bayerir2rgb_get_property;

	/* Add properties */
	g_object_class_install_property (gobject_class, PROP_DEBAYER_MODE,
						g_param_spec_enum ("debayer-mode", "Debayer mode",
						"Method to debayer data", GST_DEBAYER_MODE,
						GST_DEBAYER_MODE_DEFAULT,
						G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_PACKED,
						g_param_spec_boolean ("packed", "Packed source", "Source stream is packed",
						GST_PACKED_DEFAULT,
						G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));


	gst_element_class_set_static_metadata (gstelement_class,
							"Bayer-IR to RGB decoder for cameras (deprecated)",
							"Filter/Converter/Video",
							"Converts video/x-bayer to video/x-raw",
							"William Brack <wbrack@mmm.com.hk>");

	gst_element_class_add_pad_template (gstelement_class, gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_from_string (GST_BAYERIR_2_RGB_SRC_CAPS)));
	gst_element_class_add_pad_template (gstelement_class, gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, gst_caps_from_string (GST_BAYERIR_2_RGB_SINK_CAPS)));

	GST_BASE_TRANSFORM_CLASS (klass)->transform_caps = GST_DEBUG_FUNCPTR (gst_bayerir2rgb_transform_caps);
	GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size = GST_DEBUG_FUNCPTR (gst_bayerir2rgb_get_unit_size);
	GST_BASE_TRANSFORM_CLASS (klass)->set_caps = GST_DEBUG_FUNCPTR (gst_bayerir2rgb_set_caps);
	GST_BASE_TRANSFORM_CLASS (klass)->transform = GST_DEBUG_FUNCPTR (gst_bayerir2rgb_transform);

	GST_DEBUG_CATEGORY_INIT (gst_bayerir2rgb_debug, "bayerir2rgb", 0, "bayerir2rgb element");
}
