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
#include <string.h>
#include <gst/gst.h>
#include "gstbayerfuncs.h"

/* Test pattern generator	*/
/********************************/
void testpattern_create_pixel_grp(uint8_t mode, uint16_t x, uint16_t y, uint16_t width, uint64_t *pixgrp_0, uint64_t *pixgrp_1) {
	switch (mode) {
	case GST_BAYERIR2BAYER_TESTPATTERN_RED:
		if (y & 0x2) {
			*pixgrp_0 = 0xffff000000000000;
		} else {
			*pixgrp_0 = 0x00000000ffff0000;
		}
		*pixgrp_1 = 0x0000000000000000;
		break;
	case GST_BAYERIR2BAYER_TESTPATTERN_GREEN:
		*pixgrp_0 = 0x0000ffff0000ffff;
		*pixgrp_1 = 0xffff0000ffff0000;
		break;
	case GST_BAYERIR2BAYER_TESTPATTERN_BLUE:
		if (y & 0x2) {
			*pixgrp_0 = 0x00000000ffff0000;
		} else {
			*pixgrp_0 = 0xffff000000000000;
		}
		*pixgrp_1 = 0x0000000000000000;
		break;
	case GST_BAYERIR2BAYER_TESTPATTERN_BAR:
		if (x < (width/4)) {
			// Black
			*pixgrp_0 = 0x0000000000000000;
			*pixgrp_1 = 0x0000000000000000;
		} else if ((x >= (width/4)) && (x < (width/2))) {
			// Red
			if (y & 0x2) {
				*pixgrp_0 = 0xffff000000000000;
			} else {
				*pixgrp_0 = 0x00000000ffff0000;
			}
			*pixgrp_1 = 0x0000000000000000;
		} else if ((x >= (width/2)) && (x < ((width/4) * 3))) {
			// Green
			*pixgrp_0 = 0x0000ffff0000ffff;
			*pixgrp_1 = 0xffff0000ffff0000;
		} else {
			// Blue
			if (y & 0x2) {
				*pixgrp_0 = 0x00000000ffff0000;
			} else {
				*pixgrp_0 = 0xffff000000000000;
			}
			*pixgrp_1 = 0x0000000000000000;
		}
		break;
	default:
		*pixgrp_0 = 0x0000000000000000;
		*pixgrp_1 = 0x0000000000000000;
		break;
	}

}


/* Plugin transforms		*/
/********************************/
// Extract packed 10 bit from 5 bytes to 64 bit long
uint64_t extract_4_words_from_10bit_packed(uint8_t *src) {
	uint64_t val, ret = 0;
	uint16_t *ptr = (uint16_t *) &ret;
	const uint16_t bit_mask = ((1U<<10) - 1);

	memcpy(&val, src, sizeof(val));

	ptr[0] = val & bit_mask;
	val = val >> 10;
	ptr[1] = val & bit_mask;
	val = val >> 10;
	ptr[2] = val & bit_mask;
	val = val >> 10;
	ptr[3] = val & bit_mask;

	return ret;
}

// Do not use: This is slower than the other routine
// Extract packed 10 bit from 5 bytes to 64 bit long
//uint64_t extract_4_words_from_10bit_packed_alt(uint8_t *src) {
//	uint64_t val, ret;
//	const uint64_t bit_mask0 = ((1UL<<10) - 1) << 0;
//	const uint64_t bit_mask1 = ((1UL<<10) - 1) << 16;
//	const uint64_t bit_mask2 = ((1UL<<10) - 1) << 32;
//	const uint64_t bit_mask3 = ((1UL<<10) - 1) << 48;
//
//	memcpy(&val, src, sizeof(val));
//
//	ret = val & bit_mask0;
//	val = val << 6;
//	ret |= val & bit_mask1;
//	val = val << 6;
//	ret |= val & bit_mask2;
//	val = val << 6;
//	ret |= val & bit_mask3;
//
//	return ret;
//}

// Do not use: This is slower than the other routine
// Extract packed 10 bit from 5 bytes to 64 bit long
//uint64_t extract_4_words_from_10bit_packed_alt(uint8_t *src) {
//	uint64_t val, ret;
//	uint16_t *val_ptr = (uint16_t *) &val, *ret_ptr = (uint16_t *) &ret;
//	const uint16_t bit_mask = ((1U<<10) - 1);
//
//	memcpy(&val, src, sizeof(val));
//
//	ret_ptr[0] = val_ptr[0] & bit_mask;
//	val = val << 6;
//	ret_ptr[1] = val_ptr[1] & bit_mask;
//	val = val << 6;
//	ret_ptr[2] = val_ptr[2] & bit_mask;
//	val = val << 6;
//	ret_ptr[3] = val_ptr[3] & bit_mask;
//
//	return ret;
//}
