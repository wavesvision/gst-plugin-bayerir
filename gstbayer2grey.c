/*
 * GStreamer plugin to translate bayer IR to greyscale
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
 * SECTION:element-bayerir2grey
 * @title: bayerir2grey
 *
 * Transcodes raw camera bayer-IR to greyscale.
 */

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
#include "gstbayer2grey.h"


#define GST_CAT_DEFAULT				gst_bayerir2grey_debug

#define GST_TYPE_BAYERIR2GREY			(gst_bayerir2grey_get_type())
#define GST_BAYERIR2GREY(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BAYERIR2GREY,GstBayerIR2Grey))
#define GST_IS_BAYERIR2GREY(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BAYERIR2GREY))
#define GST_BAYERIR2GREY_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_BAYERIR2GREY,GstBayerIR2GreyClass))
#define GST_IS_BAYERIR2GREY_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_BAYERIR2GREY))
#define GST_BAYERIR2GREY_GET_CLASS(obj)		(G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_BAYERIR2GREY,GstBayerIR2GreyClass))
typedef struct _GstBayerIR2Grey			GstBayerIR2Grey;
typedef struct _GstBayerIRGreyClass		GstBayerIR2GreyClass;

typedef void (*GstBayerIR2GreyProcessFunc)	(GstBayerIR2Grey *, guint8 *, guint);


/* Plugin properties */
enum {
        PROP_0,
        PROP_COLOUR_CHANNEL,
        PROP_SWAP_ORDER,
};

/* Colour channel */
enum {
	GST_COLOUR_CHANNEL_RED,
	GST_COLOUR_CHANNEL_GREEN,
	GST_COLOUR_CHANNEL_BLUE,
	GST_COLOUR_CHANNEL_IR,
} GstColourChannel;
/* Plugin defaults */
#define GST_COLOUR_CHANNEL_DEFAULT	GST_COLOUR_CHANNEL_IR
#define GST_SWAP_ORDER_DEFAULT		FALSE

#define GST_COLOUR_CHANNEL (gst_colour_channel_get_type ())
static GType gst_colour_channel_get_type (void) {
	static GType colour_channel_type = 0;
	static const GEnumValue colour_channels[] = {
		{GST_COLOUR_CHANNEL_RED, "Extract red channel", "red"},
		{GST_COLOUR_CHANNEL_GREEN, "Extract green channel", "green"},
		{GST_COLOUR_CHANNEL_BLUE, "Extract blue channel", "blue"},
		{GST_COLOUR_CHANNEL_IR, "Extract IR channel", "ir"},
		{0, NULL, NULL}
	};

	if (!colour_channel_type) {
		colour_channel_type = g_enum_register_static ("GstColourChannel", colour_channels);
	}

	return colour_channel_type;
}

GType gst_bayerir2grey_get_type (void);

#define gst_bayerir2grey_parent_class parent_class
G_DEFINE_TYPE (GstBayerIR2Grey, gst_bayerir2grey, GST_TYPE_BASE_TRANSFORM);
GST_ELEMENT_REGISTER_DEFINE (bayerir2grey, "bayerir2grey", GST_RANK_NONE, gst_bayerir2grey_get_type ());


/* Plugin transforms		*/
/********************************/
#define PIXMASK_1	0x000000000000ffff


static void
gst_bayerir2grey_process_16b_fast (GstBayerIR2Grey * filter, uint8_t * dest, uint8_t * src) {
	uint64_t pixgrp_0, pixgrp_1;
	uint8_t *dst_ptr0, *dst_ptr1, dst_ptr_inc, dst_y_inc;
	uint8_t *src_ptr0, *src_ptr1, src_ptr_inc;
	uint16_t src_x, src_y, dst_y, pix_tmp16;
	uint8_t px_extract0, px_extract1, px_extract_cnt;

	/* Configure pointer increments */
	src_ptr_inc = 8;
	dst_ptr_inc = 2;

	/* Reset extraction pattern to the start */
	px_extract_cnt = 0;

	/* Configure destination pointer */
	dst_y = 0;
	dst_y_inc = 1;

	/* For each group of 2 lines			*/
	/************************************************/
	for (src_y = 0; src_y < filter->src_height; src_y+=2) {
		// Get pixel exraction pattern
		px_extract0 = filter->extract_pixels[px_extract_cnt];
		px_extract1 = filter->extract_pixels[px_extract_cnt+1];

		// Calculate destination y offset
		dst_ptr0 = dst_ptr1 = dest + (filter->dst_stride * dst_y);
		if ((px_extract0 > 0) && (px_extract1 > 0)) {
			dst_ptr1 += filter->dst_stride;
			dst_y_inc = 2;
		}

		// Calculate source y offset
		src_ptr0 = src + (filter->src_stride * src_y);
		src_ptr1 = src_ptr0 + filter->src_stride;

		/* For each group of 4x2 pixels			*/
		/************************************************/
		for (src_x = 0; src_x < filter->src_width; src_x+=4) {
			/* Extract pixel group				*/
			/************************************************/
			if (filter->swap_order) {
				pixgrp_0 = *((uint64_t *) src_ptr1);
				pixgrp_1 = *((uint64_t *) src_ptr0);
			} else {
				pixgrp_0 = *((uint64_t *) src_ptr0);
				pixgrp_1 = *((uint64_t *) src_ptr1);
			}

			/* Convert sample to require destination bpp	*/
			/************************************************/
			pixgrp_0 <<= filter->src_shift;
			pixgrp_1 <<= filter->src_shift;

			/* Extract pixels (line 0)	*/
			/********************************/
			if (px_extract0 > 0) {
				if (px_extract0 & 0x1) {
					pix_tmp16 = (pixgrp_0 >> 0) & PIXMASK_1;
					*((uint16_t *) dst_ptr0) = pix_tmp16;
					dst_ptr0 += dst_ptr_inc;
				}
				if (px_extract0 & 0x2) {
					pix_tmp16 = (pixgrp_0 >> 16) & PIXMASK_1;
					*((uint16_t *) dst_ptr0) = pix_tmp16;
					dst_ptr0 += dst_ptr_inc;
				}
				if (px_extract0 & 0x4) {
					pix_tmp16 = (pixgrp_0 >> 32) & PIXMASK_1;
					*((uint16_t *) dst_ptr0) = pix_tmp16;
					dst_ptr0 += dst_ptr_inc;
				}
				if (px_extract0 & 0x8) {
					pix_tmp16 = (pixgrp_0 >> 48) & PIXMASK_1;
					*((uint16_t *) dst_ptr0) = pix_tmp16;
					dst_ptr0 += dst_ptr_inc;
				}
			}

			/* Extract pixels (line 1)	*/
			/********************************/
			if (px_extract1 > 0) {
				if (px_extract1 & 0x1) {
					pix_tmp16 = (pixgrp_1 >> 0) & PIXMASK_1;
					*((uint16_t *) dst_ptr1) = pix_tmp16;
					dst_ptr1 += dst_ptr_inc;
				}
				if (px_extract1 & 0x2) {
					pix_tmp16 = (pixgrp_1 >> 16) & PIXMASK_1;
					*((uint16_t *) dst_ptr1) = pix_tmp16;
					dst_ptr1 += dst_ptr_inc;
				}
				if (px_extract1 & 0x4) {
					pix_tmp16 = (pixgrp_1 >> 32) & PIXMASK_1;
					*((uint16_t *) dst_ptr1) = pix_tmp16;
					dst_ptr1 += dst_ptr_inc;
				}
				if (px_extract1 & 0x8) {
					pix_tmp16 = (pixgrp_1 >> 48) & PIXMASK_1;
					*((uint16_t *) dst_ptr1) = pix_tmp16;
					dst_ptr1 += dst_ptr_inc;
				}
			}

			/* Increment pointers (row)		*/
			/****************************************/
			src_ptr0 += src_ptr_inc;
			src_ptr1 += src_ptr_inc;
		}

		/* Increment pointers (line)		*/
		/****************************************/
		dst_y += dst_y_inc;
		px_extract_cnt = (px_extract_cnt + 2) & 0x2;
	}
}

static GstFlowReturn
gst_bayerir2grey_transform (GstBaseTransform * base, GstBuffer * inbuf, GstBuffer * outbuf) {
	GstBayerIR2Grey *filter = GST_BAYERIR2GREY (base);
	GstMapInfo map_in, map_out;

	GST_DEBUG ("%s: enter", __func__);

	if (!gst_buffer_map (inbuf, &map_in, GST_MAP_READ))
		goto map_in_failed;

	if (!gst_buffer_map (outbuf, &map_out, GST_MAP_WRITE))
		goto map_out_failed;

	/* Process frame */
	gst_bayerir2grey_process_16b_fast (filter, map_out.data, map_in.data);

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
gst_bayerir2grey_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec) {
	GstBayerIR2Grey *filter = GST_BAYERIR2GREY (object);

	GST_DEBUG ("%s: enter", __func__);

	switch (prop_id) {
		case PROP_COLOUR_CHANNEL:
			gint colour_channel = g_value_get_enum (value);
			GST_OBJECT_LOCK (filter);
			filter->colour_channel = colour_channel;
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
gst_bayerir2grey_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec) {
	GstBayerIR2Grey *filter = GST_BAYERIR2GREY (object);

	GST_DEBUG ("%s: enter", __func__);

	switch (prop_id) {
		case PROP_COLOUR_CHANNEL:
			GST_OBJECT_LOCK (filter);
			g_value_set_enum (value, filter->colour_channel);
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

static void
get_bayerir2grey_cfg_pixel_extraction_order (GstBaseTransform * base) {
	GstBayerIR2Grey *filter = GST_BAYERIR2GREY (base);
	uint px_red1 = 0, px_green1 = 0, px_blue1 = 0, px_green2 = 0, px_ir2 = 0;

	GST_DEBUG ("%s: enter", __func__);

	/* Pixel format changes when read in reverse order */
	if (filter->swap_order) {
		/* So update pixel format */
		switch (filter->src_format) {
			case GST_BAYERIR_2_GREY_FORMAT_BGGR:
				filter->src_format = GST_BAYERIR_2_GREY_FORMAT_GRBG;
				break;
			case GST_BAYERIR_2_GREY_FORMAT_GBRG:
				filter->src_format = GST_BAYERIR_2_GREY_FORMAT_RGGB;
				break;
			case GST_BAYERIR_2_GREY_FORMAT_GRBG:
				filter->src_format = GST_BAYERIR_2_GREY_FORMAT_BGGR;
				break;
			case GST_BAYERIR_2_GREY_FORMAT_RGGB:
				filter->src_format = GST_BAYERIR_2_GREY_FORMAT_GBRG;
				break;
			default:
				GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Incorrect colour channel selected"));
				break;
		}
	}

	/* Create pixel maskes */
	switch (filter->src_format) {
		case GST_BAYERIR_2_GREY_FORMAT_BGGR:
			GST_DEBUG_OBJECT (filter, "input format: BGGR");
			px_red1		= 0b0100;
			px_green1	= 0b1010;
			px_blue1	= 0b0001;
			px_green2	= 0b0101;
			px_ir2	 	= 0b1010;
			break;
		case GST_BAYERIR_2_GREY_FORMAT_GBRG:
			GST_DEBUG_OBJECT (filter, "input format: GBRG");
			px_red1		= 0b1000;
			px_green1	= 0b0101;
			px_blue1	= 0b0010;
			px_green2	= 0b1010;
			px_ir2	 	= 0b0101;
			break;
		case GST_BAYERIR_2_GREY_FORMAT_GRBG:
			GST_DEBUG_OBJECT (filter, "input format: GRBG");
			px_red1		= 0b0010;
			px_green1	= 0b0101;
			px_blue1	= 0b1000;
			px_green2	= 0b1010;
			px_ir2	 	= 0b0101;
			break;
		case GST_BAYERIR_2_GREY_FORMAT_RGGB:
			GST_DEBUG_OBJECT (filter, "input format: RGGB");
			px_red1		= 0b0001;
			px_green1	= 0b1010;
			px_blue1	= 0b0100;
			px_green2	= 0b0101;
			px_ir2	 	= 0b1010;
			break;
		default:
			GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Incorrect colour channel selected"));
			break;
	}

	switch (filter->colour_channel) {
		case GST_COLOUR_CHANNEL_RED:
			GST_DEBUG_OBJECT (filter, "extracting channel: red");
			filter->extract_pixels[0] = px_red1;
			filter->extract_pixels[1] = 0;
			filter->extract_pixels[2] = px_blue1;	/* red/blue swap every other line */
			filter->extract_pixels[3] = 0;
			break;
		case GST_COLOUR_CHANNEL_GREEN:
			GST_DEBUG_OBJECT (filter, "extracting channel: green");
			filter->extract_pixels[0] = px_green1;
			filter->extract_pixels[1] = px_green2;
			filter->extract_pixels[2] = px_green1;
			filter->extract_pixels[3] = px_green2;
			break;
		case GST_COLOUR_CHANNEL_BLUE:
			GST_DEBUG_OBJECT (filter, "extracting channel: blue");
			filter->extract_pixels[0] = px_blue1;
			filter->extract_pixels[1] = 0;
			filter->extract_pixels[2] = px_red1;	/* red/blue swap every other line */
			filter->extract_pixels[3] = 0;
			break;
		case GST_COLOUR_CHANNEL_IR:
			GST_DEBUG_OBJECT (filter, "extracting channel: ir");
			filter->extract_pixels[0] = 0;
			filter->extract_pixels[1] = px_ir2;
			filter->extract_pixels[2] = 0;
			filter->extract_pixels[3] = px_ir2;
			break;
		default:
			GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Incorrect colour channel selected"));
			break;
	}
}

static void
get_bayerir2grey_cfg_pixel_extraction_caps (GstBaseTransform * base, GstStructure *structure, GstPadDirection direction) {
	GstBayerIR2Grey *filter = GST_BAYERIR2GREY (base);
	GValue h_val = { 0, };
	GValue w_val = { 0, };
	gint height, width;

	GST_DEBUG ("%s: enter", __func__);

	g_value_init(&h_val, G_TYPE_INT);
	g_value_init(&w_val, G_TYPE_INT);

	gst_structure_get_int (structure, "width", &width);
	gst_structure_get_int (structure, "height", &height);

	if (direction == GST_PAD_SINK) {
		switch (filter->colour_channel) {
			case GST_COLOUR_CHANNEL_RED:
				height = height / 2;
				width = width / 4;
				break;
			case GST_COLOUR_CHANNEL_GREEN:
				height = height;
				width = width / 2;
				break;
			case GST_COLOUR_CHANNEL_BLUE:
				height = height / 2;
				width = width / 4;
				break;
			case GST_COLOUR_CHANNEL_IR:
				height = height / 2;
				width = width / 2;
				break;
			default:
				GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Incorrect colour channel selected"));
				break;
		}
	} else {
		switch (filter->colour_channel) {
			case GST_COLOUR_CHANNEL_RED:
				height = height * 2;
				width = width * 4;
				break;
			case GST_COLOUR_CHANNEL_GREEN:
				height = height;
				width = width * 2;
				break;
			case GST_COLOUR_CHANNEL_BLUE:
				height = height * 2;
				width = width * 4;
				break;
			case GST_COLOUR_CHANNEL_IR:
				height = height * 2;
				width = width * 2;
				break;
			default:
				GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Incorrect colour channel selected"));
				break;
		}
	}

	g_value_set_int(&h_val, height);
	g_value_set_int(&w_val, width);
	gst_structure_set_value (structure, "height", &h_val);
	gst_structure_set_value (structure, "width", &w_val);
	GST_DEBUG_OBJECT (filter, "updated output parameters to %ux%u", width, height);
}

static GstCaps *
gst_bayerir2grey_transform_caps (GstBaseTransform * base, GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps) {
	GstBayerIR2Grey *filter;
	GstCaps *res_caps, *tmp_caps;
	GstStructure *structure;
	guint i, caps_size;

	GST_DEBUG ("%s: enter", __func__);
	GST_DEBUG ("%s: direction: %s	caps: %" GST_PTR_FORMAT "	filter_caps: %" GST_PTR_FORMAT,
						 __func__, (direction == GST_PAD_SINK) ? "sink" : "source", caps, filter_caps);

	filter = GST_BAYERIR2GREY (base);

	res_caps = gst_caps_copy (caps);
	caps_size = gst_caps_get_size (res_caps);
	for (i = 0; i < caps_size; i++) {
		structure = gst_caps_get_structure (res_caps, i);
		if (direction == GST_PAD_SINK) {
			gst_structure_set_name (structure, "video/x-raw");
			gst_structure_remove_field (structure, "format");
			/* Update the output parameters */
			get_bayerir2grey_cfg_pixel_extraction_caps(base, structure, direction);
		} else {
			gst_structure_set_name (structure, "video/x-bayer");
			gst_structure_remove_fields (structure, "format", "colorimetry", "chroma-site", NULL);
			/* Update the output parameters */
			if (filter_caps)
				get_bayerir2grey_cfg_pixel_extraction_caps(base, structure, direction);
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
gst_bayerir2grey_set_caps (GstBaseTransform * base, GstCaps * incaps, GstCaps * outcaps) {
	GstBayerIR2Grey *filter = GST_BAYERIR2GREY (base);
	GstStructure *structure;
	const char *format;
	GstVideoInfo info;

	GST_DEBUG ("%s: enter", __func__);
	GST_DEBUG ("in caps %" GST_PTR_FORMAT " out caps %" GST_PTR_FORMAT, incaps, outcaps);

	/* Store caps pointers (so as to be able to identify which is which later) */
	filter->caps_in = incaps;
	filter->caps_out = outcaps;

	/* Get sink configuration */
	structure = gst_caps_get_structure (incaps, 0);

	gst_structure_get_int (structure, "width", &filter->src_width);
	gst_structure_get_int (structure, "height", &filter->src_height);

	format = gst_structure_get_string (structure, "format");
	if (g_str_has_prefix (format, "bggr")) {
		filter->src_format = GST_BAYERIR_2_GREY_FORMAT_BGGR;
	} else if (g_str_has_prefix (format, "gbrg")) {
		filter->src_format = GST_BAYERIR_2_GREY_FORMAT_GBRG;
	} else if (g_str_has_prefix (format, "grbg")) {
		filter->src_format = GST_BAYERIR_2_GREY_FORMAT_GRBG;
	} else if (g_str_has_prefix (format, "rggb")) {
		filter->src_format = GST_BAYERIR_2_GREY_FORMAT_RGGB;
	} else {
		GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Unknown bayer format!"));
		return FALSE;
	}
	if (strlen (format) == 4) {							/* 8bit bayer */
		filter->src_bpp = 8;
	} else if (strlen (format) == 8) {						/* 10/12/14/16 le/be bayer */
		filter->src_bpp = (gint) g_ascii_strtoull (format + 4, NULL, 10);
		if (filter->src_bpp & 1) {						/* odd filter->src_bpp bayer formats not supported */
			GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Odd source bits per pixel value not supported!"));
			return FALSE;
		}
		if (filter->src_bpp < 10 || filter->src_bpp > 16) {			/* bayer 10,12,14,16 only */
			GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Source bits per pixel values <8 or >16 not supported!"));
			return FALSE;
		}

		if (g_str_has_suffix (format, "le")) {
			filter->bigendian = 0;
		} else if (g_str_has_suffix (format, "be")) {
			filter->bigendian = 1;
		} else {
			GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Endianness not specified!"));
			return FALSE;
		}
	} else {
		GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Couldn't decode format string!"));
		return FALSE;
	}

	// Get source configuration
	structure = gst_caps_get_structure (outcaps, 0);

	gst_structure_get_int (structure, "width", &filter->dst_width);
	gst_structure_get_int (structure, "height", &filter->dst_height);
	filter->dst_bpp = 16;								/* Output BPP always 16 bit */

	/* Configure pixel extraction order */
	get_bayerir2grey_cfg_pixel_extraction_order(base);

	/* Calculate number of bits to shift source */
	if (filter->dst_bpp < filter->src_bpp) {
		GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Downsampling unsupported!"));
		return FALSE;
	} else {
		filter->src_shift = filter->dst_bpp - filter->src_bpp;
	}

	/* Set source stride (line length in bytes) */
	filter->src_stride = GST_ROUND_UP_4 (filter->src_width) * DIV_ROUND_UP (filter->src_bpp, 8);
	filter->dst_stride = GST_ROUND_UP_4 (filter->dst_width) * DIV_ROUND_UP (filter->dst_bpp, 8);

	gst_video_info_from_caps (&info, outcaps);
	filter->info = info;

	GST_DEBUG_OBJECT (base, "set_caps: output width: %u", info.width);
	GST_DEBUG_OBJECT (base, "set_caps: output height: %u", info.height);
	GST_DEBUG_OBJECT (base, "set_caps: output size: %" G_GSIZE_FORMAT, info.size);

	return TRUE;
}

static gboolean
gst_bayerir2grey_get_unit_size (GstBaseTransform * base, GstCaps * caps, gsize * size) {
	GstStructure *structure;
	GstBayerIR2Grey *filter;
	const char *name;

	GST_DEBUG ("%s: enter", __func__);

	structure = gst_caps_get_structure (caps, 0);
	filter = GST_BAYERIR2GREY (base);

	name = gst_structure_get_name (structure);
	/* Our name must be either video/x-bayer video/x-raw */
	if (strcmp (name, "video/x-raw") == 0) {
		/* For output, calculate according to format */
		*size = GST_ROUND_UP_4 (filter->dst_width) * filter->dst_height * DIV_ROUND_UP (filter->dst_bpp, 8);
		GST_DEBUG_OBJECT (base, "get_unit_size: output size %" G_GSIZE_FORMAT, *size);
		return TRUE;
	} else {
		*size = GST_ROUND_UP_4 (filter->src_width) * filter->src_height * DIV_ROUND_UP (filter->src_bpp, 8);
		GST_DEBUG_OBJECT (base, "get_unit_size: input size %" G_GSIZE_FORMAT, *size);
		return TRUE;
	}

	GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL), ("Incomplete caps, some required field missing"));
	return FALSE;
}

static void
gst_bayerir2grey_reset (GstBayerIR2Grey * filter) {
	GST_DEBUG ("%s: enter", __func__);

	filter->caps_in = NULL;
	filter->caps_out = NULL;

	filter->src_width = 0;
	filter->src_height = 0;
	filter->src_format = 0;
	filter->src_bpp = 0;
	filter->src_shift = 0;
	filter->src_stride = 0;

	filter->dst_width = 0;
	filter->dst_height = 0;
	filter->dst_bpp = 0;
	filter->dst_stride = 0;

	filter->bigendian = 0;
	filter->swap_order = GST_SWAP_ORDER_DEFAULT;
	filter->colour_channel = GST_COLOUR_CHANNEL_DEFAULT;
	//filter->extract_pixels = { 0, 0, 0, 0};

	gst_video_info_init (&filter->info);
}

/* Plugin init                  */
/********************************/
static void
gst_bayerir2grey_init (GstBayerIR2Grey * filter) {
	gst_bayerir2grey_reset (filter);
	gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), FALSE);
}

static void
gst_bayerir2grey_class_init (GstBayerIR2GreyClass * klass) {
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	gobject_class->set_property = gst_bayerir2grey_set_property;
	gobject_class->get_property = gst_bayerir2grey_get_property;

	/* Add properties */
	g_object_class_install_property (gobject_class, PROP_COLOUR_CHANNEL,
						g_param_spec_enum ("colour-channel", "Colour channel",
						"Colour channel to extract", GST_COLOUR_CHANNEL,
						GST_COLOUR_CHANNEL_DEFAULT,
						G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, PROP_SWAP_ORDER,
						g_param_spec_boolean ("swap-order", "Swap order", "Swap order lines are processed in",
						GST_SWAP_ORDER_DEFAULT,
						G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));


	gst_element_class_set_static_metadata (gstelement_class,
							"Bayer-IR to greyscale decoder for camera sensors",
							"Filter/Converter/Video",
							"Converts video/x-bayer to video/x-raw",
							"Charles Burgoyne <charlesb@wavesvision.com>");

	gst_element_class_add_pad_template (gstelement_class, gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_from_string (GST_BAYERIR_2_GREY_SRC_CAPS)));
	gst_element_class_add_pad_template (gstelement_class, gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, gst_caps_from_string (GST_BAYERIR_2_GREY_SINK_CAPS)));

	GST_BASE_TRANSFORM_CLASS (klass)->transform_caps = GST_DEBUG_FUNCPTR (gst_bayerir2grey_transform_caps);
	GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size = GST_DEBUG_FUNCPTR (gst_bayerir2grey_get_unit_size);
	GST_BASE_TRANSFORM_CLASS (klass)->set_caps = GST_DEBUG_FUNCPTR (gst_bayerir2grey_set_caps);
	GST_BASE_TRANSFORM_CLASS (klass)->transform = GST_DEBUG_FUNCPTR (gst_bayerir2grey_transform);

	GST_DEBUG_CATEGORY_INIT (gst_bayerir2grey_debug, "bayerir2grey", 0, "bayerir2grey element");
}
