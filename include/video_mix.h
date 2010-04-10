/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2010 Brian J. Crowell <brian@fluggo.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "framework.h"

#if !defined(FLUGGO_VIDEO_MIX_H)
#define FLUGGO_VIDEO_MIX_H

void video_copy_frame_alpha_f32( rgba_f32_frame *out, rgba_f32_frame *in, float alpha );
void video_mix_cross_f32_pull( rgba_f32_frame *out, video_source *a, int frame_a, video_source *b, int frame_b, float mix_b );
void video_mix_cross_f32( rgba_f32_frame *out, rgba_f32_frame *a, rgba_f32_frame *b, float mix_b );
void video_mix_over_f32( rgba_f32_frame *out, rgba_f32_frame *a, rgba_f32_frame *b, float mix_a, float mix_b );

#endif
