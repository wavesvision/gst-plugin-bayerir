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

#ifndef __GST_BAYERIR_2_GREY_H__
#define __GST_BAYERIR_2_GREY_H__

#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <stdlib.h>
#include <stdint.h>


#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(s,v) (((s) + ((v)-1)) / (v))
#endif

GST_DEBUG_CATEGORY_STATIC (gst_bayerir2grey_debug);

enum {
	GST_BAYERIR_2_GREY_FORMAT_BGGR = 0,
	GST_BAYERIR_2_GREY_FORMAT_GBRG,
	GST_BAYERIR_2_GREY_FORMAT_GRBG,
	GST_BAYERIR_2_GREY_FORMAT_RGGB
};

struct _GstBayerIR2Grey {
	GstBaseTransform basetransform;

	/* < private > */
	GstVideoInfo info;
	GstCaps *caps_in;			/* Store pointers to caps structures so they can be identified later */
	GstCaps *caps_out;

	/* Source parameters */
	int src_width;
	int src_height;
	int src_format;
	int src_bpp;				/* bits per pixel, 8/10/12/14/16 */
	int src_shift;				/* number of bits to shift source to match output */
	int src_stride;				/* source frame line length */

	/* Destination parameters */
	int dst_width;
	int dst_height;
	int dst_bpp;				/* bits per pixel, 8/10/12/14/16 */
	int dst_stride;				/* destination frame line length */

	int bigendian;
	uint colour_channel;			/* colour channel to extract */
	gboolean swap_order;			/* Whether to swap the processing order of lines */
	uint8_t extract_pixels[4];		/* pixels to extract on a line */
};

struct _GstBayerIRGreyClass {
	GstBaseTransformClass parent;
};

#define BAYER_CAPS_GEN(mask, bits, endian)	\
	" "#mask#bits#endian

#define BAYER_CAPS_ORD(bits, endian)		\
	BAYER_CAPS_GEN(bggr, bits, endian)","	\
	BAYER_CAPS_GEN(rggb, bits, endian)","	\
	BAYER_CAPS_GEN(grbg, bits, endian)","	\
	BAYER_CAPS_GEN(gbrg, bits, endian)

#define BAYER_CAPS_BITS(bits)			\
	BAYER_CAPS_ORD(bits, le)","		\
	BAYER_CAPS_ORD(bits, be)

#define BAYER_CAPS_ALL				\
	BAYER_CAPS_BITS(10)","			\
	BAYER_CAPS_BITS(12)","			\
	BAYER_CAPS_BITS(14)","			\
	BAYER_CAPS_BITS(16)

#define GST_BAYERIR_2_GREY_SINK_CAPS "video/x-bayer,format=(string){" BAYER_CAPS_ALL " }, "\
	"width=(int)[1,MAX],height=(int)[1,MAX],framerate=(fraction)[0/1,MAX]"

#define GST_BAYERIR_2_GREY_SRC_CAPS \
	GST_VIDEO_CAPS_MAKE ("{ GRAY16_LE, GRAY16_BE }")


#endif /* __GST_BAYERIR_2_GREY_H__ */
