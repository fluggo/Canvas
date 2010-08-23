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

#include <string.h>
#include "audio_mix.h"

EXPORT void
audio_copy_frame( audio_frame *out, const audio_frame *in, int offset ) {
    g_assert( out );
    g_assert( out->frameData );
    g_assert( in );
    g_assert( in->frameData );

    out->currentMinSample = max(out->fullMinSample, in->currentMinSample - offset);
    out->currentMaxSample = min(out->fullMaxSample, in->currentMaxSample - offset);

    if( out->currentMaxSample < out->currentMinSample )
        return;

    if( out->channelCount == in->channelCount ) {
        // Easiest case: a direct copy
        memcpy( audio_get_sample( out, out->currentMinSample, 0 ),
            audio_get_sample( in, out->currentMinSample + offset, 0 ),
            sizeof(float) * in->channelCount * (out->currentMaxSample - out->currentMinSample + 1) );
        return;
    }

    for( int out_sample = out->currentMinSample; out_sample <= out->currentMaxSample; out_sample++ ) {
        for( int channel = 0; channel < out->channelCount; channel++ ) {
            *audio_get_sample( out, out_sample, channel ) =
                (channel < in->channelCount) ? *audio_get_sample( in, out_sample + offset, channel ) : 0.0f;
        }
    }
}

EXPORT void
audio_copy_frame_attenuate( audio_frame *out, const audio_frame *in, int offset, float factor ) {
    // BJC: It occurs to me that this is the absolute basic one-source audio function. Start here if you need one.
    if( factor == 0.0f ) {
        out->currentMinSample = 0;
        out->currentMaxSample = -1;
        return;
    }

    if( factor == 1.0f ) {
        audio_copy_frame( out, in, offset );
        return;
    }

    g_assert( out );
    g_assert( out->frameData );
    g_assert( in );
    g_assert( in->frameData );

    out->currentMinSample = max(out->fullMinSample, in->currentMinSample - offset);
    out->currentMaxSample = min(out->fullMaxSample, in->currentMaxSample - offset);

    if( out->currentMaxSample < out->currentMinSample )
        return;

    for( int out_sample = out->currentMinSample; out_sample <= out->currentMaxSample; out_sample++ ) {
        for( int channel = 0; channel < out->channelCount; channel++ ) {
            *audio_get_sample( out, out_sample, channel ) =
                (channel < in->channelCount) ? *audio_get_sample( in, out_sample + offset, channel ) * factor : 0.0f;
        }
    }
}

EXPORT void
audio_mix_add( audio_frame *out, const audio_frame *a, float mix_a, const audio_frame *b, float mix_b ) {
    if( mix_a == 0.0f ) {
        audio_copy_frame_attenuate( out, b, 0, mix_b );
        return;
    }

    if( mix_b == 0.0f ) {
        audio_copy_frame_attenuate( out, a, 0, mix_a );
        return;
    }

    g_assert( out );
    g_assert( out->frameData );
    g_assert( a );
    g_assert( a->frameData );
    g_assert( b );
    g_assert( b->frameData );

    // Remember that we could have out == a or out == b
    const int out_min_sample = max(out->fullMinSample, min(a->currentMinSample, b->currentMinSample));
    const int out_max_sample = min(out->fullMaxSample, max(a->currentMaxSample, b->currentMaxSample));

    const int inner_min = max(min(max(a->currentMinSample, b->currentMinSample), min(a->currentMaxSample, b->currentMaxSample)), out_min_sample);
    const int inner_max = min(max(max(a->currentMinSample, b->currentMinSample), min(a->currentMaxSample, b->currentMaxSample)), out_max_sample);

    const audio_frame * const left = (a->currentMinSample < b->currentMinSample) ? a : b,
        *right = (a->currentMaxSample > b->currentMaxSample) ? a : b;

    const float mix_left = (left == a) ? mix_a : mix_b,
        mix_right = (right == a) ? mix_a : mix_b;

    out->currentMinSample = out_min_sample;
    out->currentMaxSample = out_max_sample;

    if( out->currentMaxSample < out->currentMinSample )
        return;

    // Left (one frame only)
    for( int sample = out->currentMinSample; sample < inner_min; sample++ ) {
        for( int channel = 0; channel < out->channelCount; channel++ ) {
            *audio_get_sample( out, sample, channel ) =
                (channel < left->channelCount) ? *audio_get_sample( left, sample, channel ) * mix_left : 0.0f;
        }
    }

    // Middle (both or neither)
    if( inner_max < inner_min ) {
        for( int sample = inner_max + 1; sample <= inner_min - 1; sample++ ) {
            for( int channel = 0; channel < out->channelCount; channel++ ) {
                *audio_get_sample( out, sample, channel ) = 0.0f;
            }
        }
    }
    else {
        for( int sample = inner_min; sample <= inner_max; sample++ ) {
            for( int channel = 0; channel < out->channelCount; channel++ ) {
                *audio_get_sample( out, sample, channel ) =
                    ((channel < a->channelCount) ? *audio_get_sample( a, sample, channel ) * mix_a : 0.0f) +
                    ((channel < b->channelCount) ? *audio_get_sample( b, sample, channel ) * mix_b : 0.0f);
            }
        }
    }

    // Right (one frame only)
    for( int sample = inner_max + 1; sample <= out->currentMaxSample; sample++ ) {
        for( int channel = 0; channel < out->channelCount; channel++ ) {
            *audio_get_sample( out, sample, channel ) =
                (channel < right->channelCount) ? *audio_get_sample( right, sample, channel ) * mix_right : 0.0f;
        }
    }
}

EXPORT void
audio_mix_add_pull( audio_frame *out, const audio_source *a, int offset_a, float mix_a, const audio_source *b, int offset_b, float mix_b ) {
    g_assert( out );

    if( mix_b == 0.0f ) {
        if( mix_a == 0.0f )
            return;

        // Adjust out's parameters
        out->fullMinSample -= offset_a;
        out->fullMaxSample -= offset_a;

        audio_get_frame( a, out );

        out->fullMinSample += offset_a;
        out->fullMaxSample += offset_a;
        out->currentMinSample += offset_a;
        out->currentMaxSample += offset_a;

        if( mix_a != 1.0f ) {
            // Apply the mix factor directly
            for( float *sample = audio_get_sample( out, out->currentMinSample, 0 );
                    sample < audio_get_sample( out, out->currentMaxSample + 1, 0 ); sample++ ) {

                *sample *= mix_a;
            }
        }
    }
    else if( mix_a == 0.0f ) {
        if( mix_b == 0.0f ) {
            out->currentMinSample = 0;
            out->currentMaxSample = -1;
            return;
        }

        // Adjust out's parameters
        out->fullMinSample -= offset_b;
        out->fullMaxSample -= offset_b;

        audio_get_frame( b, out );

        out->fullMinSample += offset_b;
        out->fullMaxSample += offset_b;
        out->currentMinSample += offset_b;
        out->currentMaxSample += offset_b;

        if( mix_b != 1.0f ) {
            // Apply the mix factor directly
            for( float *sample = audio_get_sample( out, out->currentMinSample, 0 );
                    sample < audio_get_sample( out, out->currentMaxSample + 1, 0 ); sample++ ) {

                *sample *= mix_b;
            }
        }
    }
    else {
        // Pull A directly into "out"
        out->fullMinSample -= offset_a;
        out->fullMaxSample -= offset_a;

        audio_get_frame( a, out );

        out->fullMinSample += offset_a;
        out->fullMaxSample += offset_a;
        out->currentMinSample += offset_a;
        out->currentMaxSample += offset_a;

        if( mix_b == 0.0f ) {
            // We can skip the rest, but first we need to attenuate 
            if( mix_a != 1.0f ) {
                // Apply the mix factor directly
                for( float *sample = audio_get_sample( out, out->currentMinSample, 0 );
                        sample < audio_get_sample( out, out->currentMaxSample + 1, 0 ); sample++ ) {

                    *sample *= mix_a;
                }
            }

            return;
        }

        // Pull B into a temp frame
        audio_frame temp_frame = {
            .frameData = g_slice_alloc( sizeof(float) * (out->fullMaxSample - out->fullMinSample + 1) * out->channelCount ),
            .fullMinSample = out->fullMinSample - offset_b,
            .fullMaxSample = out->fullMaxSample - offset_b,
            .channelCount = out->channelCount
        };

        audio_get_frame( b, out );

        temp_frame.fullMinSample += offset_b;
        temp_frame.fullMaxSample += offset_b;
        temp_frame.currentMinSample += offset_b;
        temp_frame.currentMaxSample += offset_b;

        // Now mix
        audio_mix_add( out, out, mix_a, &temp_frame, mix_b );

        g_slice_free1( sizeof(float) * (out->fullMaxSample - out->fullMinSample + 1) * out->channelCount, temp_frame.frameData );
    }
}


