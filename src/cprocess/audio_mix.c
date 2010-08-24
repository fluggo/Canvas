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
audio_copy_frame_attenuate( audio_frame *out, const audio_frame *in, float factor, int offset ) {
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
audio_attenuate( audio_frame *frame, float factor ) {
    g_assert( frame );
    g_assert( frame->frameData );

    if( factor == 1.0f )
        return;

    if( factor == 0.0f ) {
        frame->currentMinSample = 0;
        frame->currentMaxSample = -1;
        return;
    }

    for( float *sample = audio_get_sample( frame, frame->currentMinSample, 0 );
            sample < audio_get_sample( frame, frame->currentMaxSample + 1, 0 ); sample++ ) {

        *sample *= factor;
    }
}

EXPORT void
audio_mix_add( audio_frame *out, float mix_out, const audio_frame *a, float mix_a, int offset ) {
    if( mix_out == 0.0f || (out->currentMaxSample < out->currentMinSample) ) {
        audio_copy_frame_attenuate( out, a, mix_a, offset );
        return;
    }

    audio_attenuate( out, mix_out );

    if( mix_a == 0.0f || (a->currentMaxSample < a->currentMinSample) )
        return;

    g_assert( a );
    g_assert( a->frameData );

    const int out_min_sample = max(out->fullMinSample, min(a->currentMinSample - offset, out->currentMinSample));
    const int out_max_sample = min(out->fullMaxSample, max(a->currentMaxSample - offset, out->currentMaxSample));

    const int inner_min = max(min(
            max(a->currentMinSample - offset, out->currentMinSample),
            min(a->currentMaxSample - offset, out->currentMaxSample)
        ), out_min_sample);
    const int inner_max = min(max(
            max(a->currentMinSample - offset, out->currentMinSample),
            min(a->currentMaxSample - offset, out->currentMaxSample)
        ), out_max_sample);

    if( out->currentMaxSample < out->currentMinSample ) {
        out->currentMinSample = out_min_sample;
        out->currentMaxSample = out_max_sample;
        return;
    }

    // Left (one frame only)
    if( a->currentMinSample - offset < out->currentMinSample ) {
        for( int sample = out_min_sample; sample < inner_min; sample++ ) {
            for( int channel = 0; channel < out->channelCount; channel++ ) {
                *audio_get_sample( out, sample, channel ) =
                    (channel < a->channelCount) ? *audio_get_sample( a, sample + offset, channel ) * mix_a : 0.0f;
            }
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
                *audio_get_sample( out, sample, channel ) +=
                    ((channel < a->channelCount) ? *audio_get_sample( a, sample + offset, channel ) * mix_a : 0.0f);
            }
        }
    }

    // Right (one frame only)
    if( a->currentMaxSample - offset > out->currentMaxSample ) {
        for( int sample = inner_max + 1; sample <= out_max_sample; sample++ ) {
            for( int channel = 0; channel < out->channelCount; channel++ ) {
                *audio_get_sample( out, sample, channel ) =
                    (channel < a->channelCount) ? *audio_get_sample( a, sample + offset, channel ) * mix_a : 0.0f;
            }
        }
    }

    out->currentMinSample = out_min_sample;
    out->currentMaxSample = out_max_sample;
}

EXPORT void
audio_mix_add_pull( audio_frame *out, float mix_out, const audio_source *a, float mix_a, int offset_a ) {
    g_assert( out );
    g_assert( out->frameData );

    if( mix_out == 0.0f || (out->currentMaxSample < out->currentMinSample) ) {
        if( mix_a == 0.0f ) {
            audio_attenuate( out, 0.0f );
            return;
        }

        // We can skip allocations, just adjust out's parameters
        out->fullMinSample += offset_a;
        out->fullMaxSample += offset_a;

        audio_get_frame( a, out );

        out->fullMinSample -= offset_a;
        out->fullMaxSample -= offset_a;
        out->currentMinSample -= offset_a;
        out->currentMaxSample -= offset_a;

        // Apply the mix factor directly
        audio_attenuate( out, mix_a );
        return;
    }

    if( mix_a == 0.0f ) {
        audio_attenuate( out, mix_out );
        return;
    }

    // Pull A into a temp frame
    audio_frame temp_frame = {
        .frameData = g_slice_alloc( sizeof(float) * (out->fullMaxSample - out->fullMinSample + 1) * out->channelCount ),
        .fullMinSample = out->fullMinSample + offset_a,
        .fullMaxSample = out->fullMaxSample + offset_a,
        .channelCount = out->channelCount
    };

    audio_get_frame( a, &temp_frame );

    // Now mix
    audio_mix_add( out, mix_out, &temp_frame, mix_a, offset_a );

    g_slice_free1( sizeof(float) * (out->fullMaxSample - out->fullMinSample + 1) * out->channelCount, temp_frame.frameData );
}


