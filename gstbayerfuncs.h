/*
 * GStreamer plugin to translate bayer IR
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

#include <stdint.h>

void testpattern_create_pixel_grp(uint8_t mode, uint16_t x, uint16_t y, uint16_t width, uint64_t *pixgrp_0, uint64_t *pixgrp_1);

uint64_t extract_4_words_from_10bit_packed(uint8_t *src);

enum {
	GST_BAYERIR2BAYER_TESTPATTERN_BLANK = 0,
	GST_BAYERIR2BAYER_TESTPATTERN_RED,
	GST_BAYERIR2BAYER_TESTPATTERN_GREEN,
	GST_BAYERIR2BAYER_TESTPATTERN_BLUE,
	GST_BAYERIR2BAYER_TESTPATTERN_BAR,
};
