/*
 * GStreamer plugin to translate bayer IR to RGB (deprecated)
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

#ifndef __GST_BAYERIR_2_RGB_H__
#define __GST_BAYERIR_2_RGB_H__


#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(s,v) (((s) + ((v)-1)) / (v))
#endif

GST_DEBUG_CATEGORY_STATIC (gst_bayerir2rgb_debug);

enum {
	GST_BAYERIR_2_RGB_FORMAT_BGGR = 0,
	GST_BAYERIR_2_RGB_FORMAT_GBRG,
	GST_BAYERIR_2_RGB_FORMAT_GRBG,
	GST_BAYERIR_2_RGB_FORMAT_RGGB
};

struct _GstBayerIR2RGB {
	GstBaseTransform basetransform;

	/* < private > */
	GstVideoInfo info;
	int width;
	int height;
	int rout_off;				/* offset for red pixel in output */
	int gout_off;				/* offset for green pixel in output */
	int bout_off;				/* offset for blue pixel in output */
	int aout_off;				/* offset for alpha pixel in output */
	int format;
	int src_bpp;				/* bits per pixel, 8/10/12/14/16 */
	int src_4pxl_bytes;			/* number of bytes that represent 4 pixels */
	int src_stride;				/* source frame line length */
	int dst_bpp;				/* bits per pixel, 32/64 */
	int bigendian;
	uint debayer_mode;
	gboolean packed;			/* Whether the source stream is packed */
};

struct _GstBayerIRRGBClass {
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

#define	GST_BAYERIR_2_RGB_SRC_CAPS	\
	GST_VIDEO_CAPS_MAKE ("{ RGB,BGR, " \
	"RGBx, xRGB, BGRx, xBGR, RGBA, ARGB, BGRA, ABGR, " \
	"RGBA64_LE, ARGB64_LE, BGRA64_LE, ABGR64_LE, " \
	"RGBA64_BE, ARGB64_BE, BGRA64_BE, ABGR64_BE }")

#define GST_BAYERIR_2_RGB_SINK_CAPS "video/x-bayer,format=(string){" BAYER_CAPS_ALL " }, "\
	"width=(int)[1,MAX],height=(int)[1,MAX],framerate=(fraction)[0/1,MAX]"


#endif /* __GST_BAYERIR_2_RGB_H__ */
