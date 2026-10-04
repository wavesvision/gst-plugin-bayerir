#
# GStreamer plugin to translate bayer IR
#
# Copyright (C) 2025 WavesVision <charlesb@wavesvision.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.
#


# Post-processing			#
#########################################
# Add 1 stream to another clamping to max
.function bayer16_orc_add_streams
.dest 2 d0 guint16
.source 2 s0 guint16
.source 2 s1 guint16
.param 2 maxval guint16
.temp 2 rslt

# Now the 2 streams
addusw rslt, s0, s1
# Check if this has rolled over maxval
minuw d0, rslt, maxval


# Subtract 1 stream from another clamping to zero
.function bayer16_orc_subtract_streams
.dest 2 d0 guint16
.source 2 s0 guint16
.source 2 s1 guint16

# Subtract the 2 values clamping to zero
subusw d0, s0, s1


# Pre-processing			#
#########################################
## Process BGRG stream into blue channel
#.function bayer16_orc_horiz_split_and_upsample_bgrg_blue_le
#.dest 8 d0 guint16
#.source 8 s0 guint16
##.accumulator 2 prev_blue guint16
#.param 2 prev_blue guint16
#.temp 4 pixsubgrp1
#.temp 4 pixsubgrp2
#.temp 2 cur_blue
#.temp 2 avg_blue_avgl
#.temp 2 avg_blue
#.temp 2 avg_blue_avgr
#
## Extract current colour
#select0ql pixsubgrp1, s0
#select0lw cur_blue, pixsubgrp1
#avguw avg_blue, cur_blue, prev_blue
#avguw avg_blue_avgl, prev_blue, avg_blue
#avguw avg_blue_avgr, avg_blue, cur_blue
#mergewl pixsubgrp1, avg_blue_avgl, avg_blue
#mergewl pixsubgrp2, avg_blue_avgr, cur_blue
#mergelq d0, pixsubgrp1, pixsubgrp2
#storew prev_blue, cur_blue


# Interpolate a line of data from two adjacent lines
.function bayer16_orc_horiz_interpolate_le
.dest 2 d0 guint16
.source 2 s0 guint16
.source 2 s1 guint16

# Average sources
avguw d0, s0, s1


# Process GIR stream into green channel (10..16 bit bayer, little endian)
.function bayer16_orc_horiz_split_and_upsample_gir_le
.dest 4 d0 guint16
.source 4 s0 guint16
.temp 4 pxlgrp_next
.temp 2 cur_green
.temp 2 avg_green

# Extract green
select0lw cur_green, s0
# Get next green
loadoffl pxlgrp_next, s0, 1
select0lw avg_green, pxlgrp_next
# Average pixels
avguw avg_green, cur_green, avg_green
# Store
mergewl d0, cur_green, avg_green


# Process IRG stream into green channel (10..16 bit bayer, little endian)
.function bayer16_orc_horiz_split_and_upsample_irg_le
.dest 4 d0 guint16
.source 4 s0 guint16
.temp 4 pxlgrp_prev
.temp 2 cur_green
.temp 2 avg_green

# Extract green
select1lw cur_green, s0
# Get previous green
loadoffl pxlgrp_prev, s0, -1
select1lw avg_green, pxlgrp_prev
# Average pixels
avguw avg_green, cur_green, avg_green
# Store
mergewl d0, avg_green, cur_green

# Compressed Bayer-IR 2 Bayer		#
#########################################
# Process compressed bayer-ir (RBGG) to standard bayer (RGGB)
.function bayerir16_orc_comp_stream_rbgg_2_rggb
.dest 4 d0 guint32
.dest 4 d1 guint32
.source 4 s0 guint32
.source 4 s1 guint32
##.temp 4 rgba0 guint32
##.temp 4 rgba1 guint32
#.temp 8 cp_src16 guint64
#.temp 4 cp_src8 guint32
#.temp 2 cp_gr guint16
#.temp 2 cp_gb guint16
#.temp 8 temp1 guint64
#.temp 8 temp2 guint64
#.temp 16 temp3 uint128
.temp 8 temp64
.temp 4 temp32

# Merge source rows (s0/s1-> RGBG)
x2 mergewl temp64, s0, s1
# Write out RG data
select0ql temp32, temp64
storel d0, temp32
# Write out GB data
select1ql temp32, temp64
swapwl temp32, temp32
storel d1, temp32

#loadpq temp0, 0xffffffffffffffff
#storeq d0, temp0
#storeq d1, temp0

#storeq d0, s0
#storeq d1, s1

## Convert to 8-bit
##x4 shruw cp_src16, s0, 2
##x4 convwb cp_src8, cp_src16
## Create RGBA pairs
##splitlw cp_gr, cp_gb, cp_src8
## Swap word order GR->RG
##swapw cp_gr, cp_gr
##swapw cp_gb, cp_gb
#
## Store RGBA pixels
##mergewl d0, cp_gr, cp_gb
##mergewl d1, cp_gr, cp_gb
#
#loadpq temp1, 0xffffffffffffffff
#loadpq temp2, 0xffffffffffffffff
##mergelq d0, temp1, temp2



# Fast methods				#
#########################################
## Process unpacked stream into RGB
#.function bayer16_orc_stream_fast_grgb_2_rgba32
#.dest 8 d0 guint64
##.dest 4 d1 guint32
#.source 8 s0 guint16
##.temp 4 rgba0 guint32
##.temp 4 rgba1 guint32
#.temp 8 cp_src16 guint64
#.temp 4 cp_src8 guint32
#.temp 2 cp_gr guint16
#.temp 2 cp_gb guint16
#.temp 8 temp1 guint64
#.temp 8 temp2 guint64
#.temp 16 temp3 uint128
#
## Convert to 8-bit
##x4 shruw cp_src16, s0, 2
##x4 convwb cp_src8, cp_src16
## Create RGBA pairs
##splitlw cp_gr, cp_gb, cp_src8
## Swap word order GR->RG
##swapw cp_gr, cp_gr
##swapw cp_gb, cp_gb
#
## Store RGBA pixels
##mergewl d0, cp_gr, cp_gb
##mergewl d1, cp_gr, cp_gb
#
#loadpq temp1, 0xffffffffffffffff
#loadpq temp2, 0xffffffffffffffff
##mergelq d0, temp1, temp2



# 8-bit merge				#
#########################################
.function bayer_orc_merge_bg_bgra
.dest 8 d guint8
.source 2 g0 guint8
.source 2 r0 guint8
.source 2 b1 guint8
.source 2 g1 guint8
.source 2 g2 guint8
.source 2 r2 guint8
.temp 4 ra
.temp 4 bg
.temp 2 r
.temp 2 g
.temp 2 t

x2 avgub r, r0, r2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 255
andw t, t, 65280
orw g, t, g
x2 mergebw bg, b1, g
x2 mergebw ra, r, 255
x2 mergewl d, bg, ra


.function bayer_orc_merge_gr_bgra
.dest 8 d guint8
.source 2 b0 guint8
.source 2 g0 guint8
.source 2 g1 guint8
.source 2 r1 guint8
.source 2 b2 guint8
.source 2 g2 guint8
.temp 4 ra
.temp 4 bg
.temp 2 b
.temp 2 g
.temp 2 t

x2 avgub b, b0, b2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 65280
andw t, t, 255
orw g, t, g
x2 mergebw bg, b, g
x2 mergebw ra, r1, 255
x2 mergewl d, bg, ra


.function bayer_orc_merge_bg_abgr
.dest 8 d guint8
.source 2 g0 guint8
.source 2 r0 guint8
.source 2 b1 guint8
.source 2 g1 guint8
.source 2 g2 guint8
.source 2 r2 guint8
.temp 4 ab
.temp 4 gr
.temp 2 r
.temp 2 g
.temp 2 t

x2 avgub r, r0, r2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 255
andw t, t, 65280
orw g, t, g
x2 mergebw ab, 255, b1
x2 mergebw gr, g, r
x2 mergewl d, ab, gr


.function bayer_orc_merge_gr_abgr
.dest 8 d guint8
.source 2 b0 guint8
.source 2 g0 guint8
.source 2 g1 guint8
.source 2 r1 guint8
.source 2 b2 guint8
.source 2 g2 guint8
.temp 4 ab
.temp 4 gr
.temp 2 b
.temp 2 g
.temp 2 t

x2 avgub b, b0, b2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 65280
andw t, t, 255
orw g, t, g
x2 mergebw ab, 255, b
x2 mergebw gr, g, r1
x2 mergewl d, ab, gr


.function bayer_orc_merge_bg_rgba
.dest 8 d guint8
.source 2 g0 guint8
.source 2 r0 guint8
.source 2 b1 guint8
.source 2 g1 guint8
.source 2 g2 guint8
.source 2 r2 guint8
.temp 4 rg
.temp 4 ba
.temp 2 r
.temp 2 g
.temp 2 t

x2 avgub r, r0, r2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 255
andw t, t, 65280
orw g, t, g
x2 mergebw rg, r, g
x2 mergebw ba, b1, 255
x2 mergewl d, rg, ba


.function bayer_orc_merge_gr_rgba
.dest 8 d guint8
.source 2 b0 guint8
.source 2 g0 guint8
.source 2 g1 guint8
.source 2 r1 guint8
.source 2 b2 guint8
.source 2 g2 guint8
.temp 4 rg
.temp 4 ba
.temp 2 b
.temp 2 g
.temp 2 t

x2 avgub b, b0, b2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 65280
andw t, t, 255
orw g, t, g
x2 mergebw rg, r1, g
x2 mergebw ba, b, 255
x2 mergewl d, rg, ba


.function bayer_orc_merge_bg_argb
.dest 8 d guint8
.source 2 g0 guint8
.source 2 r0 guint8
.source 2 b1 guint8
.source 2 g1 guint8
.source 2 g2 guint8
.source 2 r2 guint8
.temp 4 ar
.temp 4 gb
.temp 2 r
.temp 2 g
.temp 2 t

x2 avgub r, r0, r2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 255
andw t, t, 65280
orw g, t, g
x2 mergebw ar, 255, r
x2 mergebw gb, g, b1
x2 mergewl d, ar, gb


.function bayer_orc_merge_gr_argb
.dest 8 d guint8
.source 2 b0 guint8
.source 2 g0 guint8
.source 2 g1 guint8
.source 2 r1 guint8
.source 2 b2 guint8
.source 2 g2 guint8
.temp 4 ar
.temp 4 gb
.temp 2 b
.temp 2 g
.temp 2 t

x2 avgub b, b0, b2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 65280
andw t, t, 255
orw g, t, g
x2 mergebw ar, 255, r1
x2 mergebw gb, g, b
x2 mergewl d, ar, gb


# 16-bit merge				#
#########################################
.function bayer16_orc_merge_bg_bgra
.dest 8 d1 guint16
.dest 8 d2 guint16
.source 4 g0 guint8
.source 4 r0 guint8
.source 4 b1 guint8
.source 4 g1 guint8
.source 4 g2 guint8
.source 4 r2 guint8
.temp 4 r
.temp 4 g
.temp 4 t

x2 avguw r, r0, r2
x2 avguw g, g0, g2
copyl t, g1
x2 avguw g, g, t
andl g, g, 65535
andl t, t, 4294901760
orl g, t, g
x2 mergewl d1, b1, g
x2 mergewl d2, r, 65535


.function bayer16_orc_merge_gr_bgra
.dest 8 d1 guint16
.dest 8 d2 guint16
.source 4 b0 guint8
.source 4 g0 guint8
.source 4 g1 guint8
.source 4 r1 guint8
.source 4 b2 guint8
.source 4 g2 guint8
.temp 4 b
.temp 4 g
.temp 4 t

x2 avguw b, b0, b2
x2 avguw g, g0, g2
copyl t, g1
x2 avguw g, g, t
andl g, g, 4294901760
andl t, t, 65535
orl g, t, g
x2 mergewl d1, b, g
x2 mergewl d2, r1, 65535


.function bayer16_orc_merge_bg_abgr
.dest 8 d1 guint16
.dest 8 d2 guint16
.source 4 g0 guint8
.source 4 r0 guint8
.source 4 b1 guint8
.source 4 g1 guint8
.source 4 g2 guint8
.source 4 r2 guint8
.temp 4 r
.temp 4 g
.temp 4 t

x2 avguw r, r0, r2
x2 avguw g, g0, g2
copyl t, g1
x2 avguw g, g, t
andl g, g, 65535
andl t, t, 4294901760
orl g, t, g
x2 mergewl d1, 65535, b1
x2 mergewl d2, g, r


.function bayer16_orc_merge_gr_abgr
.dest 8 d1 guint16
.dest 8 d2 guint16
.source 4 b0 guint8
.source 4 g0 guint8
.source 4 g1 guint8
.source 4 r1 guint8
.source 4 b2 guint8
.source 4 g2 guint8
.temp 4 b
.temp 4 g
.temp 4 t

x2 avguw b, b0, b2
x2 avguw g, g0, g2
copyl t, g1
x2 avguw g, g, t
andl g, g, 4294901760
andl t, t, 65535
orl g, t, g
x2 mergewl d1, 65535, b
x2 mergewl d2, g, r1


.function bayer16_orc_merge_bg_rgba
.dest 8 d1 guint16
.dest 8 d2 guint16
.source 4 g0 guint8
.source 4 r0 guint8
.source 4 b1 guint8
.source 4 g1 guint8
.source 4 g2 guint8
.source 4 r2 guint8
.temp 4 r
.temp 4 g
.temp 4 t

x2 avguw r, r0, r2
x2 avguw g, g0, g2
copyl t, g1
x2 avguw g, g, t
andl g, g, 65535
andl t, t, 4294901760
orl g, t, g
x2 mergewl d1, r, g
x2 mergewl d2, b1, 65535


.function bayer16_orc_merge_gr_rgba
.dest 8 d1 guint16
.dest 8 d2 guint16
.source 4 b0 guint8
.source 4 g0 guint8
.source 4 g1 guint8
.source 4 r1 guint8
.source 4 b2 guint8
.source 4 g2 guint8
.temp 4 b
.temp 4 g
.temp 4 t

x2 avguw b, b0, b2
x2 avguw g, g0, g2
copyl t, g1
x2 avguw g, g, t
andl g, g, 4294901760
andl t, t, 65535
orl g, t, g
x2 mergewl d1, r1, g
x2 mergewl d2, b, 65535


.function bayer16_orc_merge_bg_argb
.dest 8 d1 guint16
.dest 8 d2 guint16
.source 4 g0 guint8
.source 4 r0 guint8
.source 4 b1 guint8
.source 4 g1 guint8
.source 4 g2 guint8
.source 4 r2 guint8
.temp 4 r
.temp 4 g
.temp 4 t

x2 avguw r, r0, r2
x2 avguw g, g0, g2
copyl t, g1
x2 avguw g, g, t
andl g, g, 65535
andl t, t, 4294901760
orl g, t, g
x2 mergewl d1, 65535, r
x2 mergewl d2, g, b1


.function bayer16_orc_merge_gr_argb
.dest 8 d1 guint16
.dest 8 d2 guint16
.source 4 b0 guint8
.source 4 g0 guint8
.source 4 g1 guint8
.source 4 r1 guint8
.source 4 b2 guint8
.source 4 g2 guint8
.temp 4 b
.temp 4 g
.temp 4 t

x2 avguw b, b0, b2
x2 avguw g, g0, g2
copyl t, g1
x2 avguw g, g, t
andl g, g, 4294901760
andl t, t, 65535
orl g, t, g
x2 mergewl d1, 65535, r1
x2 mergewl d2, g, b


# Re-order				#
#########################################
.function bayer16to16_orc_reorder
.dest 8 d guint8
.source 4 s1 guint32
.source 4 s2 guint32
.param 4 shift
.temp 4 u
.temp 4 v
.temp 8 q

x2 muluwl q, s1, 0xffff
x2 shrul q, q, shift
x2 convuuslw u, q
x2 muluwl q, s2, 0xffff
x2 shrul q, q, shift
x2 convuuslw v, q
mergelq d, u, v


.function bayer16to8_orc_reorder
.dest 4 d guint8
.source 4 s1 guint32
.source 4 s2 guint32
.param 4 shift
.temp 2 u
.temp 2 v
.temp 4 l

x2 shruw l, s1, shift
x2 convuuswb u, l
x2 shruw l, s2, shift
x2 convuuswb v, l
mergewl d, u, v


.function bayer8to16_orc_reorder
.dest 8 d guint8
.source 4 s guint32

x4 splatbw d, s


# Greyscale to colour			#
#########################################
.function bayer16_orc_greyscale_2_rgba64
.dest 8 d0 guint64
.source 2 s0 guint16
.temp 4 rg
.temp 4 ba
.temp 2 tmp_clr guint16

copyw tmp_clr, s0
mergewl rg, tmp_clr, tmp_clr
mergewl ba, tmp_clr, 65535
mergelq d0, rg, ba


.function bayer16_orc_greyscale_2_argb64
.dest 8 d0 guint64
.source 2 s0 guint16
.temp 4 rg
.temp 4 ba
.temp 2 tmp_clr guint16

copyw tmp_clr, s0
mergewl rg, 65535, tmp_clr
mergewl ba, tmp_clr, tmp_clr
mergelq d0, rg, ba


.function bayer16_orc_greyscale_2_rgba32
.dest 4 d0 guint32
.source 2 s0 guint16
.param 4 shift
.temp 2 tmpw_clr1 guuin16
.temp 2 tmpw_clr2 guint16
.temp 1 tmpb_clr1 guint8

shruw tmpw_clr1, s0, shift
convuuswb tmpb_clr1, tmpw_clr1
mergebw tmpw_clr1, tmpb_clr1, tmpb_clr1
mergebw tmpw_clr2, tmpb_clr1, 255
mergewl d0, tmpw_clr1, tmpw_clr2


.function bayer16_orc_greyscale_2_argb32
.dest 4 d0 guint32
.source 2 s0 guint16
.param 4 shift
.temp 2 tmpw_clr1 guuin16
.temp 2 tmpw_clr2 guint16
.temp 1 tmpb_clr1 guint8

shruw tmpw_clr1, s0, shift
convuuswb tmpb_clr1, tmpw_clr1
mergebw tmpw_clr1, 255, tmpb_clr1
mergebw tmpw_clr2, tmpb_clr1, tmpb_clr1
mergewl d0, tmpw_clr1, tmpw_clr2






#########################################
#.function bayer_orc_horiz_upsample_unaligned
#.dest 2 d0 guint8
#.dest 2 d1 guint8
#.source 2 s guint8
#.temp 2 t
#.temp 1 b
#.temp 1 c
#.temp 1 d
#.temp 1 e
#
#splitwb c, b, s
#loadoffw t, s, 1
#splitwb e, d, t
#avgub e, c, e
#mergebw d0, c, e
#avgub b, b, d
#mergebw d1, b, d


#.function bayer_orc_horiz_upsample
#.dest 2 d0 guint8
#.dest 2 d1 guint8
#.source 2 s guint8
#.temp 2 t
#.temp 1 b
#.temp 1 c
#.temp 1 d
#.temp 1 e
#
#loadoffw t, s, -1
#select1wb b, t
#splitwb d, c, s
#loadoffw t, s, 1
#select0wb e, t
#avgub e, c, e
#mergebw d0, c, e
#avgub b, b, d
#mergebw d1, b, d


#.function bayer16_orc_horiz_upsample_be
#.dest 4 d0 guint16
#.dest 4 d1 guint16
#.source 4 s guint16
#.temp 4 t
#.temp 2 b
#.temp 2 c
#.temp 2 d
#.temp 2 e
#
#splitlw c, b, s
#swapw b, b
#swapw c, c
#loadoffl t, s, 1
#splitlw e, d, t
#swapw d, d
#swapw e, e
#avguw e, c, e
#mergewl d0, c, e
#avguw b, b, d
#mergewl d1, b, d


