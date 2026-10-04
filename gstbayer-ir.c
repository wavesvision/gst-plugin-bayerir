/*
 * GStreamer plugins to translate bayer IR
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstbayerelements.h"
#include "gstbayer2bayer.h"
#include "gstbayer2grey.h"
#ifdef BUILD_DEPRECATED
#include "gstbayer2rgb.h"
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
	gboolean ret = FALSE;

	GST_DEBUG_CATEGORY_INIT (gst_bayerir2bayer_debug, "bayerir2bayer", 0, "BayerIR2Bayer Element");
	ret |= GST_ELEMENT_REGISTER (bayerir2bayer, plugin);

	GST_DEBUG_CATEGORY_INIT (gst_bayerir2grey_debug, "bayerir2grey", 0, "BayerIR2Grey Element");
	ret |= GST_ELEMENT_REGISTER (bayerir2grey, plugin);

#ifdef BUILD_DEPRECATED
	GST_DEBUG_CATEGORY_INIT (gst_bayerir2rgb_debug, "bayerir2rgb", 0, "BayerIR2RGB Element");
	ret |= GST_ELEMENT_REGISTER (bayerir2rgb, plugin);
#endif

	return ret;
}

/* FIXME: these are normally defined by the GStreamer build system.
   If you are creating an element to be included in gst-plugins-*,
   remove these, as they're always defined.  Otherwise, edit as
   appropriate for your external plugin package. */
#ifndef VERSION
#define VERSION "0.3.0"
#endif
#ifndef PACKAGE
#define PACKAGE "bayer-ir"
#endif
#ifndef GST_LICENSE
#define GST_LICENSE "LGPL"
#endif
#ifndef GST_PACKAGE_NAME
#define GST_PACKAGE_NAME "bayer-ir"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "N/A"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    bayer_ir,
    "Elements to convert Bayer-IR images",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
