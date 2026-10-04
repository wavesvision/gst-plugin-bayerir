/*
 * GStreamer plugin to translate bayer IR to standard bayer
 *
 * Copyright (C) 2025 WavesVision <charlesb@wavesvision.com>
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
 */

/*
 * SECTION:element-bayerir2bayer
 * @title: bayerir2bayer
 *
 * Transcodes raw camera bayer-IR to standard bayer.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <string.h>
#include <stdlib.h>

//#ifdef HAVE_STDINT_H
#include <stdint.h>
//#endif

#include "gstbayerelements.h"
#include "gstbayerfuncs.h"
#include "gstbayerorc.h"
#include "gstbayer2bayer.h"


/* Testing patern generator */
//#define GST_BAYERIR2BAYER_TESTPATTERN_MODE	GST_BAYERIR2BAYER_TESTPATTERN_BAR


#define GST_CAT_DEFAULT gst_bayerir2bayer_debug

#define GST_TYPE_BAYERIR2BAYER			(gst_bayerir2bayer_get_type())
#define GST_BAYERIR2BAYER(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BAYERIR2BAYER,GstBayerIR2Bayer))
#define GST_IS_BAYERIR2BAYER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BAYERIR2BAYER))
#define GST_BAYERIR2BAYER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_BAYERIR2BAYER,GstBayerIR2BayerClass))
#define GST_IS_BAYERIR2BAYER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_BAYERIR2BAYER))
#define GST_BAYERIR2BAYER_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_BAYERIR2BAYER,GstBayerIR2BayerClass))
typedef struct _GstBayerIR2Bayer		GstBayerIR2Bayer;
typedef struct _GstBayerIRBayerClass		GstBayerIR2BayerClass;

typedef void (*GstBayerIR2BayerProcessFunc)	(GstBayerIR2Bayer *, guint8 *, guint);


/* Plugin properties */
enum {
	PROP_0,
	PROP_BAYER_MODE,
	PROP_FILTER_IR,
	PROP_FILTER_IR_R,
	PROP_FILTER_IR_G,
	PROP_FILTER_IR_B,
	PROP_PACKED,
	PROP_SWAP_ORDER,
};

/* Bayer modes */
enum {
	GST_BAYER_MODE_RGBIR_FAST,
	GST_BAYER_MODE_RGBIR_LAVG,
	GST_BAYER_MODE_RGBIR_TEST,
} GstBayerMode;

/* IR filter */
enum {
	GST_IR_FILTER_NONE,
	GST_IR_FILTER_NIGHT,
	GST_IR_FILTER_DAY,
	GST_IR_FILTER_CUSTOM_ADD,
	GST_IR_FILTER_CUSTOM_SUBTRACT,
} GstIRFilter;

/* Standard IR filter parameters: day/night */
uint filter_ir_std[2][3] = {
	{ 7, 4, 3 },
	{ 6, 3, 0 },
};

#define GST_BAYER_MODE_DEFAULT		GST_BAYER_MODE_RGBIR_FAST
#define GST_FILTER_IR_DEFAULT		GST_IR_FILTER_NONE
#define GST_FILTER_IR_RED_DEFAULT	0
#define GST_FILTER_IR_GREEN_DEFAULT	0
#define GST_FILTER_IR_BLUE_DEFAULT	0
#define GST_FILTER_IR_MIN		0
#define GST_FILTER_IR_MAX		10
#define GST_PACKED_DEFAULT		FALSE
#define GST_SWAP_ORDER_DEFAULT		FALSE

#define GST_BAYER_MODE (gst_bayer_mode_get_type ())
static GType
gst_bayer_mode_get_type (void) {
	static GType bayer_mode_type = 0;
	static const GEnumValue mode_types[] = {
		{GST_BAYER_MODE_RGBIR_FAST, "Bayer RGB-IR: Fast, but crude", "rgbir-fast"},
		{GST_BAYER_MODE_RGBIR_LAVG, "Bayer RGB-IR: Linear average", "rgbir-lavg"},
		{GST_BAYER_MODE_RGBIR_TEST, "Bayer RGB-IR: Testing", "rgbir-test"},
		{0, NULL, NULL}
	};

	if (!bayer_mode_type) {
		bayer_mode_type = g_enum_register_static ("GstBayerMode", mode_types);
	}

	return bayer_mode_type;
}

#define GST_IR_FILTER (gst_ir_filter_get_type ())
static GType
gst_ir_filter_get_type (void) {
	static GType ir_filter_type = 0;
	static const GEnumValue mode_types[] = {
		{GST_IR_FILTER_NONE, "IR Filter", "none"},
		{GST_IR_FILTER_DAY, "IR Filter: Standard daylight filter", "day"},
		{GST_IR_FILTER_NIGHT, "IR Filter: Standard nightime filter", "night"},
		{GST_IR_FILTER_CUSTOM_ADD, "IR Filter: Custom additive filter", "add"},
		{GST_IR_FILTER_CUSTOM_SUBTRACT, "IR Filter: Custom subtractive filter", "subtract"},
		{0, NULL, NULL}
	};

	if (!ir_filter_type) {
		ir_filter_type = g_enum_register_static ("GstIRFilter", mode_types);
	}

	return ir_filter_type;
}

GType gst_bayerir2bayer_get_type (void);

#define gst_bayerir2bayer_parent_class parent_class
G_DEFINE_TYPE (GstBayerIR2Bayer, gst_bayerir2bayer, GST_TYPE_BASE_TRANSFORM);
GST_ELEMENT_REGISTER_DEFINE (bayerir2bayer, "bayerir2bayer", GST_RANK_NONE, gst_bayerir2bayer_get_type ());


/* FixMe: Shouldn't need theses		*/
/****************************************/
//#define GST_BAYERIR2RGB_SRC_FRAME_SIZE			6469632
#define GST_BAYERIR2RGB_SRC_FRAME_SIZE			1748992
#define GST_BAYERIR2RGB_SRC_FRAME_BYTES_PER_LINE	3328
#define GST_BAYERIR2RGB_SRC_FRAME_PIXELS_PER_LINE	2662


/* Plugin transforms		*/
/********************************/
// R values are rounded down (for shifting)
#define PIXMASK_1	0x000000000000ffff
#define PIXMASK_1R	0x000000000000fffe
#define PIXMASK_2	0x00000000ffff0000
#define PIXMASK_2R	0x00000000fffe0000
#define PIXMASK_3	0x0000ffff00000000
#define PIXMASK_3R	0x0000fffe00000000
#define PIXMASK_31	0x0000ffff0000ffff
#define PIXMASK_31R	0x0000fffe0000fffe
#define PIXMASK_321	0x0000ffffffffffff
#define PIXMASK_4	0xffff000000000000
#define PIXMASK_4R	0xfffe000000000000
#define PIXMASK_42	0xffff0000ffff0000
#define PIXMASK_421	0xffff0000ffffffff
#define PIXMASK_431	0xffffffff0000ffff
#define PIXMASK_432	0xffffffffffff0000
#define PIXMASK_4321R	0xfffefffefffefffe


//static int
//gst_bayerir2bayer_get_src_format(GstBayerIR2Bayer * filter) {
//	/* Pixel format changes when read in reverse order */
//	if (filter->swap_order) {
//		/* So update pixel format */
//		switch (filter->src_format) {
//			case GST_BAYERIR_2_BAYER_FORMAT_BGGR:
//				return GST_BAYERIR_2_BAYER_FORMAT_GRBG;
//				break;
//			case GST_BAYERIR_2_BAYER_FORMAT_GBRG:
//				return GST_BAYERIR_2_BAYER_FORMAT_RGGB;
//				break;
//			case GST_BAYERIR_2_BAYER_FORMAT_GRBG:
//				return GST_BAYERIR_2_BAYER_FORMAT_BGGR;
//				break;
//			case GST_BAYERIR_2_BAYER_FORMAT_RGGB:
//				return GST_BAYERIR_2_BAYER_FORMAT_GBRG;
//				break;
//			default:
//				return -1;
//				break;
//		}
//	}
//
//
//	return filter->src_format;
//}

static void
gst_bayerir2bayer_process_16b_fast (GstBayerIR2Bayer * filter, uint8_t * dest, uint8_t * src) {
	uint64_t pixgrp_0, pixgrp_1, pixgrp_ir_r, pixgrp_ir_g, pixgrp_ir_b, pixgrp_tmp;
	uint64_t *ir_ptr0_head = NULL, *ir_ptr0, *ir_ptr1_head = NULL, *ir_ptr1;
	uint8_t *dst_ptr0_head, *dst_ptr0, *dst_ptr1_head, *dst_ptr1, dst_ptr_inc;
	uint8_t *src_ptr0, *src_ptr1, src_ptr_inc;
	uint16_t src_x, src_y;
	uint16_t max_value = 0x03ff << filter->src_shift;

	// Configure pointer increments
	if (filter->packed) {
		src_ptr_inc = filter->src_4pxl_bytes;
	} else {
		src_ptr_inc = 8;
        }
	dst_ptr_inc = 8;

	// Allocate buffers for IR data if it needs to be filtered
	if (filter->filter_ir) {
		ir_ptr0_head = ir_ptr0 = g_malloc (filter->src_stride);
		ir_ptr1_head = ir_ptr1 = g_malloc (filter->src_stride);
	}

	/* For each group of 2 lines			*/
	/************************************************/
	for (src_y = 0; src_y < filter->height; src_y+=2) {
		// Calculate destination y offset
		dst_ptr0_head = dst_ptr0 = dest + (filter->dst_stride * src_y);
		dst_ptr1_head = dst_ptr1 = dst_ptr0 + filter->dst_stride;
		// Calculate source y offset
		src_ptr0 = src + (filter->src_stride * src_y);
		src_ptr1 = src_ptr0 + filter->src_stride;

		// Reset filter IR pointers
		ir_ptr0 = ir_ptr0_head;
		ir_ptr1 = ir_ptr1_head;

		/* For each group of 4x2 pixels			*/
		/************************************************/
		for (src_x = 0; src_x < filter->width; src_x+=4) {
			/* Extract colour values			*/
			/************************************************/
			// Check whether source is packed
#ifdef GST_BAYERIR2BAYER_TESTPATTERN_MODE
			testpattern_create_pixel_grp(GST_BAYERIR2BAYER_TESTPATTERN_MODE, src_x, src_y, filter->width, &pixgrp_0, &pixgrp_1);
#else
			if (filter->packed) {
				if (filter->swap_order) {
					// Unpack data
					pixgrp_0 = extract_4_words_from_10bit_packed(src_ptr1);
					pixgrp_1 = extract_4_words_from_10bit_packed(src_ptr0);
				} else {
					// Unpack data
					pixgrp_0 = extract_4_words_from_10bit_packed(src_ptr0);
					pixgrp_1 = extract_4_words_from_10bit_packed(src_ptr1);
				}
			} else {
				if (filter->swap_order) {
					pixgrp_0 = *((uint64_t *) src_ptr1);
					pixgrp_1 = *((uint64_t *) src_ptr0);
				} else {
					pixgrp_0 = *((uint64_t *) src_ptr0);
					pixgrp_1 = *((uint64_t *) src_ptr1);
				}
			}
#endif

			/* Transformation depends on bayer format	*/
			/************************************************/
			switch (filter->src_format) {
				case GST_BAYERIR_2_BAYER_FORMAT_BGGR:
					// Check if IR filtering is enabled
					if (filter->filter_ir) {
						/* Get 1/10 of the average of the IR values */
						pixgrp_tmp	=	(pixgrp_1 & PIXMASK_2) >> 17;
						pixgrp_tmp	+=	(pixgrp_1 & PIXMASK_4) >> 49;
						pixgrp_tmp	/=	 10;

						/* If the output is being shifted, then IR transform needs to be to */
						if (filter->src_shift) pixgrp_tmp <<= filter->src_shift;

						/* Create IR colour components */
						pixgrp_ir_r	=	(pixgrp_tmp) * filter->filter_ir_r;		// Create red IR value
						pixgrp_ir_r	|=	pixgrp_ir_r << 32;
						pixgrp_ir_g	=	(pixgrp_tmp) * filter->filter_ir_g;		// Create green IR value
						pixgrp_ir_g	|=	pixgrp_ir_g << 32;
						pixgrp_ir_b	=	(pixgrp_tmp) * filter->filter_ir_b;		// Create blue IR value
						pixgrp_ir_b	|=	pixgrp_ir_b << 32;

						/* Create and save IR pixel transform groups */
						pixgrp_tmp	=	pixgrp_ir_b;					// IR transform group for line 0
						pixgrp_tmp	|=	pixgrp_ir_g << 16;
						*ir_ptr0	=	pixgrp_tmp;
						pixgrp_tmp	=	pixgrp_ir_g;					// IR transform group for line 1
						pixgrp_tmp	|=	pixgrp_ir_r << 16;
						*ir_ptr1	=	pixgrp_tmp;

						/* increment pointers */
						ir_ptr0++;
						ir_ptr1++;
					}

					goto gst_bayerir2bayer_process_16b_test_transform_bggr_rggb;
				case GST_BAYERIR_2_BAYER_FORMAT_RGGB:
					// Check if IR filtering is enabled
					if (filter->filter_ir) {
						/* Get 1/10 of the average of the IR values */
						pixgrp_tmp	=	(pixgrp_1 & PIXMASK_2) >> 17;
						pixgrp_tmp	+=	(pixgrp_1 & PIXMASK_4) >> 49;
						pixgrp_tmp	/=	 10;

						/* If the output is being shifted, then IR transform needs to be to */
						if (filter->src_shift) pixgrp_tmp <<= filter->src_shift;

						/* Create IR colour components */
						pixgrp_ir_r	=	(pixgrp_tmp) * filter->filter_ir_r;		// Create red IR value
						pixgrp_ir_r	|=	pixgrp_ir_r << 32;
						pixgrp_ir_g	=	(pixgrp_tmp) * filter->filter_ir_g;		// Create green IR value
						pixgrp_ir_g	|=	pixgrp_ir_g << 32;
						pixgrp_ir_b	=	(pixgrp_tmp) * filter->filter_ir_b;		// Create blue IR value
						pixgrp_ir_b	|=	pixgrp_ir_b << 32;

						/* Create and save IR pixel transform groups */
						pixgrp_tmp	=	pixgrp_ir_r;					// IR transform group for line 0
						pixgrp_tmp	|=	pixgrp_ir_g << 16;
						*ir_ptr0	=	pixgrp_tmp;
						pixgrp_tmp	=	pixgrp_ir_g;					// IR transform group for line 1
						pixgrp_tmp	|=	pixgrp_ir_b << 16;
						*ir_ptr1	=	pixgrp_tmp;

						/* increment pointers */
						ir_ptr0++;
						ir_ptr1++;
					}

gst_bayerir2bayer_process_16b_test_transform_bggr_rggb:
					/* Colour order switches every 2 lines	*/
					/****************************************/
					if (src_y & 0x2) {
						// 2nd Line
						////////////////
						// First copy 1st pixel to the next line as it will be overwritten
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_1;				// Copy 1nd pixel
						pixgrp_tmp	<<=	16;						// Shift 1st pixel into 2nd slot
						pixgrp_tmp	|=	pixgrp_tmp << 32;				// Replicate from the 2nd slot into the 4th
						pixgrp_1	&=	PIXMASK_31;					// Mask IR data on line 2
						pixgrp_1	|=	pixgrp_tmp;					// Combine data with the 2nd line

						// 1st Line
						////////////////
						// Next replicate 4th pixel
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_3;				// Copy 3th pixel
						pixgrp_tmp	>>=	32;						// Shift 3rd pixel into 1st slot
						pixgrp_0	&=	PIXMASK_432;					// Mask 1st pixel from line 1
						pixgrp_0	|=	pixgrp_tmp;					// Combine data with the 1st line
					} else {
						// 2nd Line
						////////////////
						// First copy 3rd pixel to the next line as it will be overwritten
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_3;				// Copy 3rd pixel
						pixgrp_tmp	<<=	16;						// Shift pixel into 4th slot
						pixgrp_tmp	|=	pixgrp_tmp >> 32;				// Replicate pixel from the 4th slot into the 2nd
						pixgrp_1	&=	PIXMASK_31;					// Mask IR data on line 2
						pixgrp_1	|=	pixgrp_tmp;					// Combine data with the 2nd line

						// 1st Line
						////////////////
						// Next replicate 1st pixel
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_1;				// Copy 1st pixel
						pixgrp_tmp	<<=	32;						// Shift 1st pixel into 3rd slot
						pixgrp_0	&=	PIXMASK_421;					// Remove 3rd pixel from line 1
						pixgrp_0	|=	pixgrp_tmp;					// Combine 3rd pixel with the 1st line
					}
					break;
				case GST_BAYERIR_2_BAYER_FORMAT_GBRG:
					// Check if IR filtering is enabled
					if (filter->filter_ir) {
						/* Get 1/10 of the average of the IR values */
						pixgrp_tmp	=	(pixgrp_1 & PIXMASK_1) >> 1;
						pixgrp_tmp	+=	(pixgrp_1 & PIXMASK_3) >> 33;
						pixgrp_tmp	/=	 10;

						/* If the output is being shifted, then IR transform needs to be to */
						if (filter->src_shift) pixgrp_tmp <<= filter->src_shift;

						/* Create IR colour components */
						pixgrp_ir_r	=	(pixgrp_tmp) * filter->filter_ir_r;		// Create red IR value
						pixgrp_ir_r	|=	pixgrp_ir_r << 32;
						pixgrp_ir_g	=	(pixgrp_tmp) * filter->filter_ir_g;		// Create green IR value
						pixgrp_ir_g	|=	pixgrp_ir_g << 32;
						pixgrp_ir_b	=	(pixgrp_tmp) * filter->filter_ir_b;		// Create blue IR value
						pixgrp_ir_b	|=	pixgrp_ir_b << 32;

						/* Create and save IR pixel transform groups */
						pixgrp_tmp	=	pixgrp_ir_g;					// IR transform group for line 0
						pixgrp_tmp	=	pixgrp_ir_b << 16;
						*ir_ptr0	=	pixgrp_tmp;

						pixgrp_tmp	|=	pixgrp_ir_r;					// IR transform group for line 1
						pixgrp_tmp	|=	pixgrp_ir_g << 16;
						*ir_ptr1	=	pixgrp_tmp;

						/* increment pointers */
						ir_ptr0++;
						ir_ptr1++;
					}

					goto gst_bayerir2bayer_process_16b_test_transform_gbrg_grbg;
				case GST_BAYERIR_2_BAYER_FORMAT_GRBG:
					// Check if IR filtering is enabled
					if (filter->filter_ir) {
						/* Get 1/10 of the average of the IR values */
						pixgrp_tmp	=	(pixgrp_1 & PIXMASK_1) >> 1;
						pixgrp_tmp	+=	(pixgrp_1 & PIXMASK_3) >> 33;
						pixgrp_tmp	/=	 10;

						/* If the output is being shifted, then IR transform needs to be to */
						if (filter->src_shift) pixgrp_tmp <<= filter->src_shift;

						/* Create IR colour components */
						pixgrp_ir_r	=	(pixgrp_tmp) * filter->filter_ir_r;		// Create red IR value
						pixgrp_ir_r	|=	pixgrp_ir_r << 32;
						pixgrp_ir_g	=	(pixgrp_tmp) * filter->filter_ir_g;		// Create green IR value
						pixgrp_ir_g	|=	pixgrp_ir_g << 32;
						pixgrp_ir_b	=	(pixgrp_tmp) * filter->filter_ir_b;		// Create blue IR value
						pixgrp_ir_b	|=	pixgrp_ir_b << 32;

						/* Create and save IR pixel transform groups */
						pixgrp_tmp	=	pixgrp_ir_g;					// IR transform group for line 0
						pixgrp_tmp	|=	pixgrp_ir_r << 16;
						*ir_ptr0	=	pixgrp_tmp;
						pixgrp_tmp	=	pixgrp_ir_b;					// IR transform group for line 1
						pixgrp_tmp	|=	pixgrp_ir_g << 16;
						*ir_ptr1	=	pixgrp_tmp;

						/* increment pointers */
						ir_ptr0++;
						ir_ptr1++;
					}

gst_bayerir2bayer_process_16b_test_transform_gbrg_grbg:
					/* Colour order switches every 2 lines	*/
					/****************************************/
					if (src_y & 0x2) {
						// 2nd Line
						////////////////
						// First copy 2nd pixel to the next line as it will be overwritten
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_2;				// Copy 2nd pixel
						pixgrp_tmp	<<=	16;						// Shift 2nd pixel into 3rd slot
						pixgrp_tmp	|=	pixgrp_tmp >> 32;				// Replicate from the 3rd slot into the 1st
						pixgrp_1	&=	PIXMASK_42;					// Mask IR data on line 2
						pixgrp_1	|=	pixgrp_tmp;					// Combine data with the 2nd line

						// 1st Line
						////////////////
						// Next replicate 4th pixel
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_4;				// Copy 4th pixel
						pixgrp_tmp	>>=	32;						// Shift 4th pixel into 2nd slot
						pixgrp_0	&=	PIXMASK_431;					// Mask 4th pixel from line 1
						pixgrp_0	|=	pixgrp_tmp;					// Combine data with the 1st line
					} else {
						// 2nd Line
						////////////////
						// First copy 4th pixel to the next line as it will be overwritten
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_4;				// Copy 4th pixel
						pixgrp_tmp	>>=	16;						// Shift pixel into 3rd slot
						pixgrp_tmp	|=	pixgrp_tmp >> 32;				// Replicate pixel from the 3rd slot into the 1st
						pixgrp_1	&=	PIXMASK_42;					// Mask IR data on line 2
						pixgrp_1	|=	pixgrp_tmp;					// Combine data with the 2nd line

						// 1st Line
						////////////////
						// Next replicate 2nd pixel
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_2;				// Copy 2nd pixel
						pixgrp_tmp	<<=	32;						// Shift 2nd pixel into 4th slot
						pixgrp_0	&=	PIXMASK_321;					// Remove 4th pixel from line 1
						pixgrp_0	|=	pixgrp_tmp;					// Combine 4th pixel with the 1st line
					}
					break;
				default:
					GST_WARNING_OBJECT (filter, "Unknown bayer code selected!");
					break;
			}

			/* Convert sample to require destination bpp	*/
			/************************************************/
			if (filter->src_shift) {
				pixgrp_0 <<= filter->src_shift;
				pixgrp_1 <<= filter->src_shift;
			}

			/* Write data to output buffer		*/
			/****************************************/
			*((uint64_t *) dst_ptr0) = pixgrp_0;
			*((uint64_t *) dst_ptr1) = pixgrp_1;

			/* Increment pointers			*/
			/****************************************/
			src_ptr0 += src_ptr_inc;
			src_ptr1 += src_ptr_inc;
			dst_ptr0 += dst_ptr_inc;
			dst_ptr1 += dst_ptr_inc;

		}

		/* IR filtering depends on bayer format		*/
		/************************************************/
		// Check if IR filtering is enabled
		if (filter->filter_ir) {
			switch (filter->filter_ir) {
				case GST_IR_FILTER_NIGHT:
				case GST_IR_FILTER_CUSTOM_ADD:
					// Add IR to RGB components
					bayer16_orc_add_streams ((guint16 *) dst_ptr0_head, (guint16 *) dst_ptr0_head, (guint16 *) ir_ptr0_head, max_value, filter->width - 1);
					bayer16_orc_add_streams ((guint16 *) dst_ptr1_head, (guint16 *) dst_ptr1_head, (guint16 *) ir_ptr1_head, max_value, filter->width - 1);

					break;

				case GST_IR_FILTER_DAY:
				case GST_IR_FILTER_CUSTOM_SUBTRACT:
					// Remove IR from RGB components
					bayer16_orc_subtract_streams ((guint16 *) dst_ptr0_head, (guint16 *) dst_ptr0_head, (guint16 *) ir_ptr0_head, filter->width - 1);
					bayer16_orc_subtract_streams ((guint16 *) dst_ptr1_head, (guint16 *) dst_ptr1_head, (guint16 *) ir_ptr1_head, filter->width - 1);

					break;
				default:
					GST_WARNING_OBJECT (filter, "Unknown bayer code selected!");
					break;
			}
		}
	}

	// Free IR filter buffers if necesssary
	if (filter->filter_ir) {
		g_free (ir_ptr0_head);
		g_free (ir_ptr1_head);
	}
}

static void
gst_bayerir2bayer_process_16b_lavg (GstBayerIR2Bayer * filter, uint8_t * dest, uint8_t * src) {
	uint64_t pixgrp_0, pixgrp_0_prev=0, pixgrp_1, pixgrp_1_prev=0, pixgrp_tmp;
	uint8_t *dst_ptr0, *dst_ptr1, dst_ptr_inc;
	uint8_t *src_ptr0, *src_ptr1, src_ptr_inc;
	int16_t src_x, src_y;

	// Configure pointer increments
	if (filter->packed) {
		src_ptr_inc = filter->src_4pxl_bytes;
	} else {
		src_ptr_inc = 8;
        }
	dst_ptr_inc = 8;

	/* For each group of 2 lines			*/
	/************************************************/
	for (src_y = 0; src_y < filter->height; src_y+=2) {
		// Calculate destination y offset
		dst_ptr0 = dest + (filter->dst_stride * src_y);
		dst_ptr1 = dst_ptr0 + filter->dst_stride;
		// Calculate source y offset
		src_ptr0 = src + (filter->src_stride * src_y);
		src_ptr1 = src_ptr0 + filter->src_stride;

		/* For each group of 4x2 pixels			*/
		/************************************************/
		for (src_x = -4; src_x < filter->width; src_x+=4) {
			/* Extract colour values			*/
			/************************************************/
#ifdef GST_BAYERIR2BAYER_TESTPATTERN_MODE
			testpattern_create_pixel_grp(GST_BAYERIR2BAYER_TESTPATTERN_MODE, src_x, src_y, filter->width, &pixgrp_0, &pixgrp_1);
#else
			// Check whether source is packed
			if (filter->packed) {
				if (filter->swap_order) {
					// Unpack data
					pixgrp_0 = extract_4_words_from_10bit_packed(src_ptr1);
					pixgrp_1 = extract_4_words_from_10bit_packed(src_ptr0);
				} else {
					// Unpack data
					pixgrp_0 = extract_4_words_from_10bit_packed(src_ptr0);
					pixgrp_1 = extract_4_words_from_10bit_packed(src_ptr1);
				}
			} else {
				if (filter->swap_order) {
					pixgrp_0 = *((uint64_t *) src_ptr1);
					pixgrp_1 = *((uint64_t *) src_ptr0);
				} else {
					pixgrp_0 = *((uint64_t *) src_ptr0);
					pixgrp_1 = *((uint64_t *) src_ptr1);
				}
			}
#endif

			/* Convert sample to require destination bpp	*/
			/************************************************/
			pixgrp_0 <<= filter->src_shift;
			pixgrp_1 <<= filter->src_shift;

			/* Colour order switches every 2 lines	*/
			/****************************************/
			if (src_y & 0x2) {
				// 2nd Line
				////////////////
				// First copy blue to the next line as it will be overwritten
				pixgrp_1	&=	PIXMASK_42;					// Mask IR data on line 2
				pixgrp_tmp	=	pixgrp_0 & PIXMASK_2;				// Copy blue
				pixgrp_tmp	>>=	16;						// Shift blue from 2->1
				pixgrp_1	|=	pixgrp_tmp;					// Combine blue data with the 2nd line
				pixgrp_tmp	&=	PIXMASK_1R;					// Copy blue (without lsb)
				pixgrp_tmp	<<=	31;						// Shift blue from 1->3 (halved for averaging)
				pixgrp_1	|=	pixgrp_tmp;					// Combine blue data with the 2nd line
				pixgrp_1_prev	+=	pixgrp_tmp;					// Combine to average with previous data

				// 1st Line
				////////////////
				// Prepare current pixel group
				pixgrp_0	&=	PIXMASK_431;					// Mask blue data from line 1
				pixgrp_tmp	=	pixgrp_0 & PIXMASK_4R;				// Copy red (without lsb)
				pixgrp_tmp	>>=	33;						// Shift red from 4->2 (halved for averaging)
				pixgrp_0	|=	pixgrp_tmp;					// Combine red data with the 1st line
				pixgrp_0	+=	(pixgrp_0_prev & PIXMASK_4R) >> 33;		// Combine to average red data
			} else {
				// 2nd Line
				////////////////
				// First copy blue to the next line as it will be overwritten
				pixgrp_1	&=	PIXMASK_42;					// Mask IR data on line 2
				pixgrp_tmp	=	pixgrp_0 & PIXMASK_4;				// Copy blue
				pixgrp_tmp	>>=	16;						// Shift blue from 4->3
				pixgrp_1	|=	pixgrp_tmp;					// Combine blue data with the 2nd line
				pixgrp_tmp	>>=	33;						// Shift blue from 3->1 (halved for averaging)
				pixgrp_1	|=	pixgrp_tmp;					// Combine blue data with the 2nd line
				pixgrp_1	+=	(pixgrp_1_prev & PIXMASK_3R) >> 33;		// Combine to average blue data

				// 1st Line
				////////////////
				// Prepare current pixel group
				pixgrp_0	&=	PIXMASK_321;					// Mask blue data from line 1
				pixgrp_tmp	=	pixgrp_0 & PIXMASK_2R;				// Copy red (without lsb)
				pixgrp_tmp	<<=	31;						// Shift red from 2->4 (halved for averaging)
				pixgrp_0	|=	pixgrp_tmp;					// Combine red data with the 1st line
				pixgrp_0_prev	+=	pixgrp_tmp;					// Combine to average with previous data
			}

			/* Need to preload previous pixel group	*/
			/****************************************/
			if (src_x < 0) 	{
				pixgrp_0_prev = pixgrp_0;
				pixgrp_1_prev = pixgrp_1;
				continue;
			}

			/* Write data to output buffer		*/
			/****************************************/
			// Check that there is valid data to write out
			if (src_x > 0) {
				*((uint64_t *) dst_ptr0) = pixgrp_0_prev;
				*((uint64_t *) dst_ptr1) = pixgrp_1_prev;
				dst_ptr0 += dst_ptr_inc;
				dst_ptr1 += dst_ptr_inc;
			}

			/* Save current pixel groups		*/
			/****************************************/
			pixgrp_0_prev = pixgrp_0;
			pixgrp_1_prev = pixgrp_1;

			/* Increment source pointers		*/
			/****************************************/
			src_ptr0 += src_ptr_inc;
			src_ptr1 += src_ptr_inc;
		}

		// FixMe: need to fixup last set of data
		*((uint64_t *) dst_ptr0) = pixgrp_0_prev;
		*((uint64_t *) dst_ptr1) = pixgrp_1_prev;
	}
}

static void
gst_bayerir2bayer_process_16b_test (GstBayerIR2Bayer * filter, uint8_t * dest, uint8_t * src) {
	uint64_t pixgrp_0, pixgrp_1, pixgrp_ir_r, pixgrp_ir_g, pixgrp_ir_b, pixgrp_tmp;
	uint64_t *ir_ptr0_head = NULL, *ir_ptr0, *ir_ptr1_head = NULL, *ir_ptr1;
	uint8_t *dst_ptr0_head, *dst_ptr0, *dst_ptr1_head, *dst_ptr1, dst_ptr_inc;
	uint8_t *src_ptr0, *src_ptr1, src_ptr_inc;
	uint16_t src_x, src_y;
	uint16_t max_value = 0x03ff << filter->src_shift;

	// Configure pointer increments
	if (filter->packed) {
		src_ptr_inc = filter->src_4pxl_bytes;
	} else {
		src_ptr_inc = 8;
        }
	dst_ptr_inc = 8;

	// Allocate buffers for IR data if it needs to be filtered
	if (filter->filter_ir) {
		ir_ptr0_head = ir_ptr0 = g_malloc (filter->src_stride);
		ir_ptr1_head = ir_ptr1 = g_malloc (filter->src_stride);
	}

	/* For each group of 2 lines			*/
	/************************************************/
	for (src_y = 0; src_y < filter->height; src_y+=2) {
		// Calculate destination y offset
		dst_ptr0_head = dst_ptr0 = dest + (filter->dst_stride * src_y);
		dst_ptr1_head = dst_ptr1 = dst_ptr0 + filter->dst_stride;
		// Calculate source y offset
		src_ptr0 = src + (filter->src_stride * src_y);
		src_ptr1 = src_ptr0 + filter->src_stride;

		// Reset filter IR pointers
		ir_ptr0 = ir_ptr0_head;
		ir_ptr1 = ir_ptr1_head;

		/* For each group of 4x2 pixels			*/
		/************************************************/
		for (src_x = 0; src_x < filter->width; src_x+=4) {
			/* Extract colour values			*/
			/************************************************/
			// Check whether source is packed
#ifdef GST_BAYERIR2BAYER_TESTPATTERN_MODE
			testpattern_create_pixel_grp(GST_BAYERIR2BAYER_TESTPATTERN_MODE, src_x, src_y, filter->width, &pixgrp_0, &pixgrp_1);
#else
			if (filter->packed) {
				if (filter->swap_order) {
					// Unpack data
					pixgrp_0 = extract_4_words_from_10bit_packed(src_ptr1);
					pixgrp_1 = extract_4_words_from_10bit_packed(src_ptr0);
				} else {
					// Unpack data
					pixgrp_0 = extract_4_words_from_10bit_packed(src_ptr0);
					pixgrp_1 = extract_4_words_from_10bit_packed(src_ptr1);
				}
			} else {
				if (filter->swap_order) {
					pixgrp_0 = *((uint64_t *) src_ptr1);
					pixgrp_1 = *((uint64_t *) src_ptr0);
				} else {
					pixgrp_0 = *((uint64_t *) src_ptr0);
					pixgrp_1 = *((uint64_t *) src_ptr1);
				}
			}
#endif

			/* Transformation depends on bayer format	*/
			/************************************************/
			switch (filter->src_format) {
				case GST_BAYERIR_2_BAYER_FORMAT_BGGR:
					// Check if IR filtering is enabled
					if (filter->filter_ir) {
						/* Get 1/10 of the average of the IR values */
						pixgrp_tmp	=	(pixgrp_1 & PIXMASK_2) >> 17;
						pixgrp_tmp	+=	(pixgrp_1 & PIXMASK_4) >> 49;
						pixgrp_tmp	/=	 10;

						/* If the output is being shifted, then IR transform needs to be to */
						if (filter->src_shift) pixgrp_tmp <<= filter->src_shift;

						/* Create IR colour components */
						pixgrp_ir_r	=	(pixgrp_tmp) * filter->filter_ir_r;		// Create red IR value
						pixgrp_ir_r	|=	pixgrp_ir_r << 32;
						pixgrp_ir_g	=	(pixgrp_tmp) * filter->filter_ir_g;		// Create green IR value
						pixgrp_ir_g	|=	pixgrp_ir_g << 32;
						pixgrp_ir_b	=	(pixgrp_tmp) * filter->filter_ir_b;		// Create blue IR value
						pixgrp_ir_b	|=	pixgrp_ir_b << 32;

						/* Create and save IR pixel transform groups */
						pixgrp_tmp	=	pixgrp_ir_b;					// IR transform group for line 0
						pixgrp_tmp	|=	pixgrp_ir_g << 16;
						*ir_ptr0	=	pixgrp_tmp;
						pixgrp_tmp	=	pixgrp_ir_g;					// IR transform group for line 1
						pixgrp_tmp	|=	pixgrp_ir_r << 16;
						*ir_ptr1	=	pixgrp_tmp;

						/* increment pointers */
						ir_ptr0++;
						ir_ptr1++;
					}

					goto gst_bayerir2bayer_process_16b_test_transform_bggr_rggb;
				case GST_BAYERIR_2_BAYER_FORMAT_RGGB:
					// Check if IR filtering is enabled
					if (filter->filter_ir) {
						/* Get 1/10 of the average of the IR values */
						pixgrp_tmp	=	(pixgrp_1 & PIXMASK_2) >> 17;
						pixgrp_tmp	+=	(pixgrp_1 & PIXMASK_4) >> 49;
						pixgrp_tmp	/=	 10;

						/* If the output is being shifted, then IR transform needs to be to */
						if (filter->src_shift) pixgrp_tmp <<= filter->src_shift;

						/* Create IR colour components */
						pixgrp_ir_r	=	(pixgrp_tmp) * filter->filter_ir_r;		// Create red IR value
						pixgrp_ir_r	|=	pixgrp_ir_r << 32;
						pixgrp_ir_g	=	(pixgrp_tmp) * filter->filter_ir_g;		// Create green IR value
						pixgrp_ir_g	|=	pixgrp_ir_g << 32;
						pixgrp_ir_b	=	(pixgrp_tmp) * filter->filter_ir_b;		// Create blue IR value
						pixgrp_ir_b	|=	pixgrp_ir_b << 32;

						/* Create and save IR pixel transform groups */
						pixgrp_tmp	=	pixgrp_ir_r;					// IR transform group for line 0
						pixgrp_tmp	|=	pixgrp_ir_g << 16;
						*ir_ptr0	=	pixgrp_tmp;
						pixgrp_tmp	=	pixgrp_ir_g;					// IR transform group for line 1
						pixgrp_tmp	|=	pixgrp_ir_b << 16;
						*ir_ptr1	=	pixgrp_tmp;

						/* increment pointers */
						ir_ptr0++;
						ir_ptr1++;
					}

gst_bayerir2bayer_process_16b_test_transform_bggr_rggb:
					/* Colour order switches every 2 lines	*/
					/****************************************/
					if (src_y & 0x2) {
						// 2nd Line
						////////////////
						// First copy 1st pixel to the next line as it will be overwritten
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_1;				// Copy 1nd pixel
						pixgrp_tmp	<<=	16;						// Shift 1st pixel into 2nd slot
						pixgrp_tmp	|=	pixgrp_tmp << 32;				// Replicate from the 2nd slot into the 4th
						pixgrp_1	&=	PIXMASK_31;					// Mask IR data on line 2
						pixgrp_1	|=	pixgrp_tmp;					// Combine data with the 2nd line

						// 1st Line
						////////////////
						// Next replicate 4th pixel
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_3;				// Copy 3th pixel
						pixgrp_tmp	>>=	32;						// Shift 3rd pixel into 1st slot
						pixgrp_0	&=	PIXMASK_432;					// Mask 1st pixel from line 1
						pixgrp_0	|=	pixgrp_tmp;					// Combine data with the 1st line
					} else {
						// 2nd Line
						////////////////
						// First copy 3rd pixel to the next line as it will be overwritten
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_3;				// Copy 3rd pixel
						pixgrp_tmp	<<=	16;						// Shift pixel into 4th slot
						pixgrp_tmp	|=	pixgrp_tmp >> 32;				// Replicate pixel from the 4th slot into the 2nd
						pixgrp_1	&=	PIXMASK_31;					// Mask IR data on line 2
						pixgrp_1	|=	pixgrp_tmp;					// Combine data with the 2nd line

						// 1st Line
						////////////////
						// Next replicate 1st pixel
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_1;				// Copy 1st pixel
						pixgrp_tmp	<<=	32;						// Shift 1st pixel into 3rd slot
						pixgrp_0	&=	PIXMASK_421;					// Remove 3rd pixel from line 1
						pixgrp_0	|=	pixgrp_tmp;					// Combine 3rd pixel with the 1st line
					}
					break;
				case GST_BAYERIR_2_BAYER_FORMAT_GBRG:
					// Check if IR filtering is enabled
					if (filter->filter_ir) {
						/* Get 1/10 of the average of the IR values */
						pixgrp_tmp	=	(pixgrp_1 & PIXMASK_1) >> 1;
						pixgrp_tmp	+=	(pixgrp_1 & PIXMASK_3) >> 33;
						pixgrp_tmp	/=	 10;

						/* If the output is being shifted, then IR transform needs to be to */
						if (filter->src_shift) pixgrp_tmp <<= filter->src_shift;

						/* Create IR colour components */
						pixgrp_ir_r	=	(pixgrp_tmp) * filter->filter_ir_r;		// Create red IR value
						pixgrp_ir_r	|=	pixgrp_ir_r << 32;
						pixgrp_ir_g	=	(pixgrp_tmp) * filter->filter_ir_g;		// Create green IR value
						pixgrp_ir_g	|=	pixgrp_ir_g << 32;
						pixgrp_ir_b	=	(pixgrp_tmp) * filter->filter_ir_b;		// Create blue IR value
						pixgrp_ir_b	|=	pixgrp_ir_b << 32;

						/* Create and save IR pixel transform groups */
						pixgrp_tmp	=	pixgrp_ir_g;					// IR transform group for line 0
						pixgrp_tmp	=	pixgrp_ir_b << 16;
						*ir_ptr0	=	pixgrp_tmp;

						pixgrp_tmp	|=	pixgrp_ir_r;					// IR transform group for line 1
						pixgrp_tmp	|=	pixgrp_ir_g << 16;
						*ir_ptr1	=	pixgrp_tmp;

						/* increment pointers */
						ir_ptr0++;
						ir_ptr1++;
					}

					goto gst_bayerir2bayer_process_16b_test_transform_gbrg_grbg;
				case GST_BAYERIR_2_BAYER_FORMAT_GRBG:
					// Check if IR filtering is enabled
					if (filter->filter_ir) {
						/* Get 1/10 of the average of the IR values */
						pixgrp_tmp	=	(pixgrp_1 & PIXMASK_1) >> 1;
						pixgrp_tmp	+=	(pixgrp_1 & PIXMASK_3) >> 33;
						pixgrp_tmp	/=	 10;

						/* If the output is being shifted, then IR transform needs to be to */
						if (filter->src_shift) pixgrp_tmp <<= filter->src_shift;

						/* Create IR colour components */
						pixgrp_ir_r	=	(pixgrp_tmp) * filter->filter_ir_r;		// Create red IR value
						pixgrp_ir_r	|=	pixgrp_ir_r << 32;
						pixgrp_ir_g	=	(pixgrp_tmp) * filter->filter_ir_g;		// Create green IR value
						pixgrp_ir_g	|=	pixgrp_ir_g << 32;
						pixgrp_ir_b	=	(pixgrp_tmp) * filter->filter_ir_b;		// Create blue IR value
						pixgrp_ir_b	|=	pixgrp_ir_b << 32;

						/* Create and save IR pixel transform groups */
						pixgrp_tmp	=	pixgrp_ir_g;					// IR transform group for line 0
						pixgrp_tmp	|=	pixgrp_ir_r << 16;
						*ir_ptr0	=	pixgrp_tmp;
						pixgrp_tmp	=	pixgrp_ir_b;					// IR transform group for line 1
						pixgrp_tmp	|=	pixgrp_ir_g << 16;
						*ir_ptr1	=	pixgrp_tmp;

						/* increment pointers */
						ir_ptr0++;
						ir_ptr1++;
					}

gst_bayerir2bayer_process_16b_test_transform_gbrg_grbg:
					/* Colour order switches every 2 lines	*/
					/****************************************/
					if (src_y & 0x2) {
						// 2nd Line
						////////////////
						// First copy 2nd pixel to the next line as it will be overwritten
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_2;				// Copy 2nd pixel
						pixgrp_tmp	<<=	16;						// Shift 2nd pixel into 3rd slot
						pixgrp_tmp	|=	pixgrp_tmp >> 32;				// Replicate from the 3rd slot into the 1st
						pixgrp_1	&=	PIXMASK_42;					// Mask IR data on line 2
						pixgrp_1	|=	pixgrp_tmp;					// Combine data with the 2nd line

						// 1st Line
						////////////////
						// Next replicate 4th pixel
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_4;				// Copy 4th pixel
						pixgrp_tmp	>>=	32;						// Shift 4th pixel into 2nd slot
						pixgrp_0	&=	PIXMASK_431;					// Mask 4th pixel from line 1
						pixgrp_0	|=	pixgrp_tmp;					// Combine data with the 1st line
					} else {
						// 2nd Line
						////////////////
						// First copy 4th pixel to the next line as it will be overwritten
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_4;				// Copy 4th pixel
						pixgrp_tmp	>>=	16;						// Shift pixel into 3rd slot
						pixgrp_tmp	|=	pixgrp_tmp >> 32;				// Replicate pixel from the 3rd slot into the 1st
						pixgrp_1	&=	PIXMASK_42;					// Mask IR data on line 2
						pixgrp_1	|=	pixgrp_tmp;					// Combine data with the 2nd line

						// 1st Line
						////////////////
						// Next replicate 2nd pixel
						pixgrp_tmp	=	pixgrp_0 & PIXMASK_2;				// Copy 2nd pixel
						pixgrp_tmp	<<=	32;						// Shift 2nd pixel into 4th slot
						pixgrp_0	&=	PIXMASK_321;					// Remove 4th pixel from line 1
						pixgrp_0	|=	pixgrp_tmp;					// Combine 4th pixel with the 1st line
					}
					break;
				default:
					GST_WARNING_OBJECT (filter, "Unknown bayer code selected!");
					break;
			}

			/* Convert sample to require destination bpp	*/
			/************************************************/
			if (filter->src_shift) {
				pixgrp_0 <<= filter->src_shift;
				pixgrp_1 <<= filter->src_shift;
			}

			/* Write data to output buffer		*/
			/****************************************/
			*((uint64_t *) dst_ptr0) = pixgrp_0;
			*((uint64_t *) dst_ptr1) = pixgrp_1;

			/* Increment pointers			*/
			/****************************************/
			src_ptr0 += src_ptr_inc;
			src_ptr1 += src_ptr_inc;
			dst_ptr0 += dst_ptr_inc;
			dst_ptr1 += dst_ptr_inc;

		}

		/* IR filtering depends on bayer format		*/
		/************************************************/
		// Check if IR filtering is enabled
		if (filter->filter_ir) {
			switch (filter->filter_ir) {
				case GST_IR_FILTER_NIGHT:
				case GST_IR_FILTER_CUSTOM_ADD:
					// Add IR to RGB components
					bayer16_orc_add_streams ((guint16 *) dst_ptr0_head, (guint16 *) dst_ptr0_head, (guint16 *) ir_ptr0_head, max_value, filter->width - 1);
					bayer16_orc_add_streams ((guint16 *) dst_ptr1_head, (guint16 *) dst_ptr1_head, (guint16 *) ir_ptr1_head, max_value, filter->width - 1);

					break;

				case GST_IR_FILTER_DAY:
				case GST_IR_FILTER_CUSTOM_SUBTRACT:
					// Remove IR from RGB components
					bayer16_orc_subtract_streams ((guint16 *) dst_ptr0_head, (guint16 *) dst_ptr0_head, (guint16 *) ir_ptr0_head, filter->width - 1);
					bayer16_orc_subtract_streams ((guint16 *) dst_ptr1_head, (guint16 *) dst_ptr1_head, (guint16 *) ir_ptr1_head, filter->width - 1);

					break;
				default:
					GST_WARNING_OBJECT (filter, "Unknown bayer code selected!");
					break;
			}
		}
	}

	// Free IR filter buffers if necesssary
	if (filter->filter_ir) {
		g_free (ir_ptr0_head);
		g_free (ir_ptr1_head);
	}
}

static GstFlowReturn
gst_bayerir2bayer_transform (GstBaseTransform * base, GstBuffer * inbuf, GstBuffer * outbuf) {
	GstBayerIR2Bayer *filter = GST_BAYERIR2BAYER (base);
	GstMapInfo map_in, map_out;

	GST_DEBUG ("transforming buffer");

	if (!gst_buffer_map (inbuf, &map_in, GST_MAP_READ))
		goto map_in_failed;

	if (!gst_buffer_map (outbuf, &map_out, GST_MAP_WRITE))
		goto map_out_failed;

	/* Process frame */
	/* Select debayer method to use */
	switch (filter->bayer_mode) {
		case GST_BAYER_MODE_RGBIR_FAST:
			gst_bayerir2bayer_process_16b_fast (filter, map_out.data, map_in.data);
			break;
		case GST_BAYER_MODE_RGBIR_LAVG:
			gst_bayerir2bayer_process_16b_lavg (filter, map_out.data, map_in.data);
			break;
		case GST_BAYER_MODE_RGBIR_TEST:
			gst_bayerir2bayer_process_16b_test (filter, map_out.data, map_in.data);
			break;
		default:
			GST_WARNING_OBJECT (filter, "Invalid debayer mode selected!");
			break;
	}

	gst_buffer_unmap (inbuf, &map_in);
	gst_buffer_unmap (outbuf, &map_out);

	return GST_FLOW_OK;

map_out_failed:
	GST_WARNING_OBJECT (base, "Could not map output buffer, skipping");
	gst_buffer_unmap (inbuf, &map_in);
map_in_failed:
	GST_WARNING_OBJECT (base, "Could not map input buffer, skipping");
	return GST_FLOW_OK;
}

/* Plugin Methods		*/
/********************************/
static void
gst_bayerir2bayer_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec) {
	GstBayerIR2Bayer *filter = GST_BAYERIR2BAYER (object);

	switch (prop_id) {
		case PROP_BAYER_MODE:
			gint bayer_mode = g_value_get_enum (value);
			GST_OBJECT_LOCK (filter);
			filter->bayer_mode = bayer_mode;
			GST_OBJECT_UNLOCK (filter);
			break;
		case PROP_FILTER_IR:
			GST_OBJECT_LOCK (filter);
			filter->filter_ir = g_value_get_enum (value);
			/* Setup standard parameters if selected */
			switch (filter->filter_ir) {
				case GST_IR_FILTER_DAY:
					filter->filter_ir_r = filter_ir_std[0][0];
					filter->filter_ir_g = filter_ir_std[0][1];
					filter->filter_ir_b = filter_ir_std[0][2];
					break;
				case GST_IR_FILTER_NIGHT:
					filter->filter_ir_r = filter_ir_std[1][0];
					filter->filter_ir_g = filter_ir_std[1][1];
					filter->filter_ir_b = filter_ir_std[1][2];
					break;
				default:
					break;
			}
			GST_OBJECT_UNLOCK (filter);
			break;
		case PROP_FILTER_IR_R:
			GST_OBJECT_LOCK (filter);
			filter->filter_ir_r = g_value_get_uint (value);
			GST_OBJECT_UNLOCK (filter);
			break;
		case PROP_FILTER_IR_G:
			GST_OBJECT_LOCK (filter);
			filter->filter_ir_g = g_value_get_uint (value);
			GST_OBJECT_UNLOCK (filter);
			break;
		case PROP_FILTER_IR_B:
			GST_OBJECT_LOCK (filter);
			filter->filter_ir_b = g_value_get_uint (value);
			GST_OBJECT_UNLOCK (filter);
			break;
		case PROP_PACKED:
			GST_OBJECT_LOCK (filter);
			filter->packed = g_value_get_boolean (value);
			GST_OBJECT_UNLOCK (filter);
			break;
		case PROP_SWAP_ORDER:
			GST_OBJECT_LOCK (filter);
			filter->swap_order = g_value_get_boolean (value);
			GST_OBJECT_UNLOCK (filter);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gst_bayerir2bayer_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec) {
	GstBayerIR2Bayer *filter = GST_BAYERIR2BAYER (object);

	switch (prop_id) {
		case PROP_BAYER_MODE:
			GST_OBJECT_LOCK (filter);
			g_value_set_enum (value, filter->bayer_mode);
			GST_OBJECT_UNLOCK (filter);
			break;
		case PROP_FILTER_IR:
			GST_OBJECT_LOCK (filter);
			g_value_set_enum (value, filter->filter_ir);
			GST_OBJECT_UNLOCK (filter);
			break;
		case PROP_FILTER_IR_R:
			GST_OBJECT_LOCK (filter);
			g_value_set_uint (value, filter->filter_ir_r);
			GST_OBJECT_UNLOCK (filter);
			break;
		case PROP_FILTER_IR_G:
			GST_OBJECT_LOCK (filter);
			g_value_set_uint (value, filter->filter_ir_g);
			GST_OBJECT_UNLOCK (filter);
			break;
		case PROP_FILTER_IR_B:
			GST_OBJECT_LOCK (filter);
			g_value_set_uint (value, filter->filter_ir_b);
			GST_OBJECT_UNLOCK (filter);
			break;
		case PROP_PACKED:
			GST_OBJECT_LOCK (filter);
			g_value_set_boolean (value, filter->packed);
			GST_OBJECT_UNLOCK (filter);
			break;
		case PROP_SWAP_ORDER:
			GST_OBJECT_LOCK (filter);
			g_value_set_enum (value, filter->swap_order);
			GST_OBJECT_UNLOCK (filter);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static gboolean
gst_bayerir2bayer_set_caps (GstBaseTransform * base, GstCaps * incaps, GstCaps * outcaps) {
	GstBayerIR2Bayer *filter = GST_BAYERIR2BAYER (base);
	GstStructure *structure;
	char *format;
	GstVideoInfo info;

	GST_DEBUG ("in caps %" GST_PTR_FORMAT " out caps %" GST_PTR_FORMAT, incaps, outcaps);

	// Store caps pointers (so as to be able to identify which is which later)
	filter->caps_in = incaps;

	// Get sink configuration
	structure = gst_caps_get_structure (incaps, 0);

	gst_structure_get_int (structure, "width", &filter->width);
	gst_structure_get_int (structure, "height", &filter->height);

	format = (char *) gst_structure_get_string (structure, "format");
	if (g_str_has_prefix (format, "bggr")) {
		filter->src_format = GST_BAYERIR_2_BAYER_FORMAT_BGGR;
	} else if (g_str_has_prefix (format, "gbrg")) {
		filter->src_format = GST_BAYERIR_2_BAYER_FORMAT_GBRG;
	} else if (g_str_has_prefix (format, "grbg")) {
		filter->src_format = GST_BAYERIR_2_BAYER_FORMAT_GRBG;
	} else if (g_str_has_prefix (format, "rggb")) {
		filter->src_format = GST_BAYERIR_2_BAYER_FORMAT_RGGB;
	} else {
		return FALSE;
	}
	if (strlen (format) == 4) {							/* 8bit bayer */
		filter->src_bpp = 8;
	} else if (strlen (format) == 8) {						/* 10/12/14/16 le/be bayer */
		filter->src_bpp = (gint) g_ascii_strtoull (format + 4, NULL, 10);
		if (filter->src_bpp & 1)						/* odd filter->src_bpp bayer formats not supported */
			return FALSE;
		if (filter->src_bpp < 10 || filter->src_bpp > 16)			/* bayer 10,12,14,16 only */
			return FALSE;

		if (g_str_has_suffix (format, "le"))
			filter->bigendian = 0;
		else if (g_str_has_suffix (format, "be"))
			filter->bigendian = 1;
		else
			return FALSE;
	} else {
		return FALSE;
	}

	/* If the order is swapped, then the output caps need updating */
	if (filter->swap_order) {
		/* So update pixel format */
		switch (filter->src_format) {
		case GST_BAYERIR_2_BAYER_FORMAT_BGGR:
			filter->src_format = GST_BAYERIR_2_BAYER_FORMAT_GRBG;
			break;
		case GST_BAYERIR_2_BAYER_FORMAT_GBRG:
			filter->src_format = GST_BAYERIR_2_BAYER_FORMAT_RGGB;
			break;
		case GST_BAYERIR_2_BAYER_FORMAT_GRBG:
			filter->src_format = GST_BAYERIR_2_BAYER_FORMAT_BGGR;
			break;
		case GST_BAYERIR_2_BAYER_FORMAT_RGGB:
			filter->src_format = GST_BAYERIR_2_BAYER_FORMAT_GBRG;
			break;
		default:
			break;
		}
	}

	// Store cap's pointer (so as to be able to identify which is which later)
	filter->caps_out = outcaps;

	// Get source configuration
	structure = gst_caps_get_structure (outcaps, 0);

	format = (char *) gst_structure_get_string (structure, "format");
	if (strlen (format) == 4) {							/* 8bit bayer */
		filter->dst_bpp = 8;
	} else if (strlen (format) == 8) {						/* 10/12/14/16 le/be bayer */
		filter->dst_bpp = (gint) g_ascii_strtoull (format + 4, NULL, 10);
		if (filter->dst_bpp & 1)						/* odd filter->dst_bpp bayer formats not supported */
			return FALSE;
		if (filter->dst_bpp < 10 || filter->dst_bpp > 16)			/* bayer 10,12,14,16 only */
			return FALSE;

		if (g_str_has_suffix (format, "le"))
			filter->bigendian = 0;
		else if (g_str_has_suffix (format, "be"))
			filter->bigendian = 1;
		else
			return FALSE;
	} else {
		return FALSE;
	}
	/* Calculate number of bits to shift source */
	if (filter->dst_bpp < filter->src_bpp) {
		GST_WARNING_OBJECT (base, "Downsampling unsupported");
		return FALSE;
	} else {
		filter->src_shift = filter->dst_bpp - filter->src_bpp;
	}

	// Set source stride (line length in bytes)
	if (filter->packed) {
		// Only 10-bit packed is supported
		if (filter->src_bpp != 10) return FALSE;

		filter->src_4pxl_bytes = 5;
		filter->src_stride = GST_BAYERIR2RGB_SRC_FRAME_BYTES_PER_LINE;
	} else {
		filter->src_4pxl_bytes = 8;
		filter->src_stride = GST_ROUND_UP_4 (filter->width) * DIV_ROUND_UP (filter->src_bpp, 8);
	}
	filter->dst_stride = GST_ROUND_UP_4 (filter->width) * DIV_ROUND_UP (filter->src_bpp, 8);

	gst_video_info_from_caps (&info, outcaps);
	filter->info = info;

	GST_DEBUG_OBJECT (base, "set_caps: output width: %u", info.width);
	GST_DEBUG_OBJECT (base, "set_caps: output height: %u", info.height);
	GST_DEBUG_OBJECT (base, "set_caps: output size: %" G_GSIZE_FORMAT, info.size);

	return TRUE;
}

static GstCaps *
gst_bayerir2bayer_transform_caps (GstBaseTransform * base, GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps) {
	GstBayerIR2Bayer *filter;
	GstCaps *res_caps, *tmp_caps;
	GstStructure *structure;
	guint i, caps_size;

	filter = GST_BAYERIR2BAYER (base);

	res_caps = gst_caps_copy (caps);
	caps_size = gst_caps_get_size (res_caps);
	for (i = 0; i < caps_size; i++) {
		structure = gst_caps_get_structure (res_caps, i);
		if (direction == GST_PAD_SINK) {
			gst_structure_set_name (structure, "video/x-bayer");
			gst_structure_remove_field (structure, "format");
		} else {
			gst_structure_set_name (structure, "video/x-bayer");
			gst_structure_remove_fields (structure, "format", "colorimetry", "chroma-site", NULL);
		}
	}
	if (filter_caps) {
		tmp_caps = res_caps;
		res_caps = gst_caps_intersect_full (filter_caps, tmp_caps, GST_CAPS_INTERSECT_FIRST);
		gst_caps_unref (tmp_caps);
	}
	GST_DEBUG_OBJECT (filter, "transformed %" GST_PTR_FORMAT " into %" GST_PTR_FORMAT, caps, res_caps);
	return res_caps;
}

static gboolean
gst_bayerir2bayer_get_unit_size (GstBaseTransform * base, GstCaps * caps, gsize * size) {
	GstStructure *structure;
	GstBayerIR2Bayer *filter;
	int width;
	int height;
//	const char *name;

	structure = gst_caps_get_structure (caps, 0);
	filter = GST_BAYERIR2BAYER (base);

	if (gst_structure_get_int (structure, "width", &width) && gst_structure_get_int (structure, "height", &height)) {
//		name = gst_structure_get_name (structure);
//		/* Our name must be either video/x-bayer video/x-raw */
//		if (strcmp (name, "video/x-raw") == 0) {
//			/* For output, calculate according to format */
//			*size = width * height * DIV_ROUND_UP (filter->dst_bpp, 8);
//			return TRUE;
//		} else {
			/* Check whether this is the input caps when handling a packed stream */
			if ((filter->packed) && (filter->caps_in == caps)) {
				/* FixMe: Need to get this from V4L2 stream */
				*size = GST_BAYERIR2RGB_SRC_FRAME_SIZE;
			} else {
				*size = GST_ROUND_UP_4 (width) * height * DIV_ROUND_UP (filter->src_bpp, 8);
			}
			GST_DEBUG_OBJECT (base, "get_unit_size: size %" G_GSIZE_FORMAT, *size);
			return TRUE;
//		}
	}
	GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Incomplete caps, some required field missing"));
	return FALSE;
}

static void
gst_bayerir2bayer_reset (GstBayerIR2Bayer * filter) {
	filter->caps_in = NULL;
	filter->caps_out = NULL;
	filter->width = 0;
	filter->height = 0;
	filter->src_format = 0;
	filter->src_bpp = 8;
	filter->src_shift = 8;
	filter->dst_bpp = 0;
	filter->src_4pxl_bytes = 0;
	filter->src_stride = 0;
	filter->dst_stride = 0;
	filter->bigendian = 0;
	filter->filter_ir = GST_FILTER_IR_DEFAULT;
	filter->filter_ir_r = 0;
	filter->filter_ir_g = 0;
	filter->filter_ir_b = 0;
	filter->packed = GST_PACKED_DEFAULT;
	filter->swap_order = GST_SWAP_ORDER_DEFAULT;
	filter->bayer_mode = GST_BAYER_MODE_DEFAULT;

	gst_video_info_init (&filter->info);
}

/* Plugin init                  */
/********************************/
static void
gst_bayerir2bayer_init (GstBayerIR2Bayer * filter) {
	gst_bayerir2bayer_reset (filter);
	gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), FALSE);
}

static void
gst_bayerir2bayer_class_init (GstBayerIR2BayerClass * klass) {
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	gobject_class->set_property = gst_bayerir2bayer_set_property;
	gobject_class->get_property = gst_bayerir2bayer_get_property;

	/* Add properties */
	g_object_class_install_property (gobject_class, PROP_BAYER_MODE,
						g_param_spec_enum ("bayer-mode", "Bayer mode",
						"Method to transcode data", GST_BAYER_MODE,
						GST_BAYER_MODE_DEFAULT,
						G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_FILTER_IR,
						g_param_spec_enum ("filter-ir", "Filter IR", "Apply IR filter to RGB components",
						GST_IR_FILTER, GST_FILTER_IR_DEFAULT,
						G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_FILTER_IR_R,
						g_param_spec_uint ("filter-ir-red", "Filter IR: Red", "IR filter red multiplier",
						GST_FILTER_IR_MIN, GST_FILTER_IR_MAX,
						GST_FILTER_IR_RED_DEFAULT,
						G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_FILTER_IR_G,
						g_param_spec_uint ("filter-ir-green", "Filter IR: Green", "IR filter green multiplier",
						GST_FILTER_IR_MIN, GST_FILTER_IR_MAX,
						GST_FILTER_IR_GREEN_DEFAULT,
						G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_FILTER_IR_B,
						g_param_spec_uint ("filter-ir-blue", "Filter IR: Blue", "IR filter blue multiplier",
						GST_FILTER_IR_MIN, GST_FILTER_IR_MAX,
						GST_FILTER_IR_BLUE_DEFAULT,
						G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_PACKED,
						g_param_spec_boolean ("packed", "Packed source", "Source stream is packed",
						GST_PACKED_DEFAULT,
						G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_SWAP_ORDER,
						g_param_spec_boolean ("swap-order", "Swap order", "Swap order lines are processed in",
						GST_SWAP_ORDER_DEFAULT,
						G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));


	gst_element_class_set_static_metadata (gstelement_class,
							"Bayer-IR to Bayer decoder for camera sensors",
							"Filter/Converter/Video",
							"Converts video/x-bayer to video/x-bayer",
							"Charles Burgoyne <charlesb@wavesvision.com>");

	gst_element_class_add_pad_template (gstelement_class, gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_from_string (GST_BAYERIR_2_BAYER_SRC_CAPS)));
	gst_element_class_add_pad_template (gstelement_class, gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, gst_caps_from_string (GST_BAYERIR_2_BAYER_SINK_CAPS)));

	GST_BASE_TRANSFORM_CLASS (klass)->transform_caps = GST_DEBUG_FUNCPTR (gst_bayerir2bayer_transform_caps);
	GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size = GST_DEBUG_FUNCPTR (gst_bayerir2bayer_get_unit_size);
	GST_BASE_TRANSFORM_CLASS (klass)->set_caps = GST_DEBUG_FUNCPTR (gst_bayerir2bayer_set_caps);
	GST_BASE_TRANSFORM_CLASS (klass)->transform = GST_DEBUG_FUNCPTR (gst_bayerir2bayer_transform);

	GST_DEBUG_CATEGORY_INIT (gst_bayerir2bayer_debug, "bayerir2bayer", 0, "bayerir2bayer element");
}
