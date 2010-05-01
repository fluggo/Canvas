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

#if !defined(FLUGGO_VIDEO_SCALE_H)
#define FLUGGO_VIDEO_SCALE_H

#include "framework.h"

#if defined(__cplusplus)
extern "C" {
#endif

void video_scale_bilinear_f32( rgba_f32_frame *target, v2f target_point, rgba_f32_frame *source, v2f source_point, v2f factors );
void video_scale_bilinear_f32_pull( rgba_f32_frame *target, v2f target_point, video_source *source, int frame, box2i *source_rect, v2f source_point, v2f factors );

#if defined(__cplusplus)
}
#endif

#endif

