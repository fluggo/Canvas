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

/*
    Function: audio_copy_frame
    Copy a frame into another frame (already allocated) with a given offset.

    out - Destination frame.
    in - Source frame.
    offset - Offset, in samples, of the source frame relative to the destination frame.
        An offset of 500, for example, would copy source sample 500 to destination sample
        0, 501 to 1, and so on.
*/
void audio_copy_frame( audio_frame *out, const audio_frame *in, int offset );

/*
    Function: audio_copy_frame
    Copy a frame into another frame (already allocated) with a given offset and attenuation.

    out - Destination frame.
    in - Source frame.
    offset - Offset, in samples, of the source frame relative to the destination frame.
        An offset of 500, for example, would copy source sample 500 to destination sample
        0, 501 to 1, and so on.
    factor - Factor to multiply input samples by. Specify 1.0 for a direct copy.
*/
void audio_copy_frame_attenuate( audio_frame *out, const audio_frame *in, int offset, float factor );

/*
    Function: audio_mix_add
    Adds two audio frames.
*/
void audio_mix_add( audio_frame *out, const audio_frame *a, float mix_a, const audio_frame *b, float mix_b );

void audio_mix_add_pull( audio_frame *out, const audio_source *a, int offset_a, float mix_a, const audio_source *b, int offset_b, float mix_b );

#endif

