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
#include "framework.h"

EXPORT void
audio_copy_frame( audio_frame *out, const audio_frame *in, int offset ) {
    g_assert( out );
    g_assert( out->data );
    g_assert( in );
    g_assert( in->data );

    out->current_min_sample = max(out->full_min_sample, in->current_min_sample - offset);
    out->current_max_sample = min(out->full_max_sample, in->current_max_sample - offset);

    if( out->current_max_sample < out->current_min_sample )
        return;

    if( out->channels == in->channels ) {
        // Easiest case: a direct copy
        memcpy( audio_get_sample( out, out->current_min_sample, 0 ),
            audio_get_sample( in, out->current_min_sample + offset, 0 ),
            sizeof(float) * in->channels * (out->current_max_sample - out->current_min_sample + 1) );
        return;
    }

    for( int out_sample = out->current_min_sample; out_sample <= out->current_max_sample; out_sample++ ) {
        for( int channel = 0; channel < out->channels; channel++ ) {
            *audio_get_sample( out, out_sample, channel ) =
                (channel < in->channels) ? *audio_get_sample( in, out_sample + offset, channel ) : 0.0f;
        }
    }
}

EXPORT void
audio_copy_frame_attenuate( audio_frame *out, const audio_frame *in, float factor, int offset ) {
    // BJC: It occurs to me that this is the absolute basic one-source audio function. Start here if you need one.
    if( factor == 0.0f ) {
        out->current_min_sample = 0;
        out->current_max_sample = -1;
        return;
    }

    if( factor == 1.0f ) {
        audio_copy_frame( out, in, offset );
        return;
    }

    g_assert( out );
    g_assert( out->data );
    g_assert( in );
    g_assert( in->data );

    out->current_min_sample = max(out->full_min_sample, in->current_min_sample - offset);
    out->current_max_sample = min(out->full_max_sample, in->current_max_sample - offset);

    if( out->current_max_sample < out->current_min_sample )
        return;

    for( int out_sample = out->current_min_sample; out_sample <= out->current_max_sample; out_sample++ ) {
        for( int channel = 0; channel < out->channels; channel++ ) {
            *audio_get_sample( out, out_sample, channel ) =
                (channel < in->channels) ? *audio_get_sample( in, out_sample + offset, channel ) * factor : 0.0f;
        }
    }
}

EXPORT void
audio_attenuate( audio_frame *frame, float factor ) {
    g_assert( frame );
    g_assert( frame->data );

    if( factor == 1.0f )
        return;

    if( factor == 0.0f ) {
        frame->current_min_sample = 0;
        frame->current_max_sample = -1;
        return;
    }

    for( float *sample = audio_get_sample( frame, frame->current_min_sample, 0 );
            sample < audio_get_sample( frame, frame->current_max_sample + 1, 0 ); sample++ ) {

        *sample *= factor;
    }
}

EXPORT void
audio_mix_add( audio_frame *out, const audio_frame *a, float mix_a, int offset ) {
    if( out->current_max_sample < out->current_min_sample ) {
        audio_copy_frame_attenuate( out, a, mix_a, offset );
        return;
    }

    if( mix_a == 0.0f || (a->current_max_sample < a->current_min_sample) )
        return;

    g_assert( a );
    g_assert( a->data );

    const int out_min_sample = max(out->full_min_sample, min(a->current_min_sample - offset, out->current_min_sample));
    const int out_max_sample = min(out->full_max_sample, max(a->current_max_sample - offset, out->current_max_sample));

    const int inner_min = max(min(
            max(a->current_min_sample - offset, out->current_min_sample),
            min(a->current_max_sample - offset, out->current_max_sample)
        ), out_min_sample);
    const int inner_max = min(max(
            max(a->current_min_sample - offset, out->current_min_sample),
            min(a->current_max_sample - offset, out->current_max_sample)
        ), out_max_sample);

    if( out->current_max_sample < out->current_min_sample ) {
        out->current_min_sample = out_min_sample;
        out->current_max_sample = out_max_sample;
        return;
    }

    // Left (one frame only)
    if( a->current_min_sample - offset < out->current_min_sample ) {
        for( int sample = out_min_sample; sample < inner_min; sample++ ) {
            for( int channel = 0; channel < out->channels; channel++ ) {
                *audio_get_sample( out, sample, channel ) =
                    (channel < a->channels) ? *audio_get_sample( a, sample + offset, channel ) * mix_a : 0.0f;
            }
        }
    }

    // Middle (both or neither)
    if( inner_max < inner_min ) {
        for( int sample = inner_max + 1; sample <= inner_min - 1; sample++ ) {
            for( int channel = 0; channel < out->channels; channel++ ) {
                *audio_get_sample( out, sample, channel ) = 0.0f;
            }
        }
    }
    else {
        for( int sample = inner_min; sample <= inner_max; sample++ ) {
            for( int channel = 0; channel < out->channels; channel++ ) {
                *audio_get_sample( out, sample, channel ) +=
                    ((channel < a->channels) ? *audio_get_sample( a, sample + offset, channel ) * mix_a : 0.0f);
            }
        }
    }

    // Right (one frame only)
    if( a->current_max_sample - offset > out->current_max_sample ) {
        for( int sample = inner_max + 1; sample <= out_max_sample; sample++ ) {
            for( int channel = 0; channel < out->channels; channel++ ) {
                *audio_get_sample( out, sample, channel ) =
                    (channel < a->channels) ? *audio_get_sample( a, sample + offset, channel ) * mix_a : 0.0f;
            }
        }
    }

    out->current_min_sample = out_min_sample;
    out->current_max_sample = out_max_sample;
}

EXPORT void
audio_mix_add_pull( audio_frame *out, const audio_source *a, float mix_a, int offset_a ) {
    g_assert( out );
    g_assert( out->data );

    if( out->current_max_sample < out->current_min_sample ) {
        if( mix_a == 0.0f ) {
            audio_attenuate( out, 0.0f );
            return;
        }

        // We can skip allocations, just adjust out's parameters
        out->full_min_sample += offset_a;
        out->full_max_sample += offset_a;

        audio_get_frame( a, out );

        out->full_min_sample -= offset_a;
        out->full_max_sample -= offset_a;
        out->current_min_sample -= offset_a;
        out->current_max_sample -= offset_a;

        // Apply the mix factor directly
        audio_attenuate( out, mix_a );
        return;
    }

    if( mix_a == 0.0f )
        return;

    // Pull A into a temp frame
    audio_frame temp_frame = {
        .data = g_slice_alloc( sizeof(float) * (out->full_max_sample - out->full_min_sample + 1) * out->channels ),
        .full_min_sample = out->full_min_sample + offset_a,
        .full_max_sample = out->full_max_sample + offset_a,
        .channels = out->channels
    };

    audio_get_frame( a, &temp_frame );

    // Now mix
    audio_mix_add( out, &temp_frame, mix_a, offset_a );

    g_slice_free1( sizeof(float) * (out->full_max_sample - out->full_min_sample + 1) * out->channels, temp_frame.data );
}


