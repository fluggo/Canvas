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

#if !defined(FLUGGO_COLOR_H)
#define FLUGGO_COLOR_H

#include "framework.h"

#if defined(__cplusplus)
extern "C" {
#endif

void video_transfer_rec709_to_linear_scene( const half *in, half *out, size_t count );
void video_transfer_rec709_to_linear_display( const half *in, half *out, size_t count );
void video_transfer_linear_to_sRGB( const half *in, half *out, size_t count );

#if defined(__cplusplus)
}
#endif

#endif

