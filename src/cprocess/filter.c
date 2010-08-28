/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2009-10 Brian J. Crowell <brian@fluggo.com>

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

#include <math.h>
#include "framework.h"

EXPORT void
filter_createTriangle( float sub, float offset, fir_filter *filter ) {
    // sub = f'/f where f is the original sampling rate (you can assume 1) and f' is the new sampling rate
    // sub = 4/1 is upsampling by a factor of four, sub = 1/4 is downsampling by the same

    // For upsampling, the filter we're coming up with is y(x) = 1 - (1/sub) * abs(x - offset)
    // For downsampling (sub < 1), it's y(x) = 1.0f - sub * abs(x - offset) scaled to sum to unity
    // Upsample goes to zero at x = offset +/- sub, down at x = offset +- (1/sub)

    g_assert(filter);
    g_assert(sub > 0.0f);

    const bool down = sub < 1.0f;
    const float width = down ? (1.0f / sub) : sub;

    float leftEdge = ceilf(offset - width);
    float rightEdge = floorf(offset + width);

    if( leftEdge == offset - width )
        leftEdge++;

    if( rightEdge == offset + width )
        rightEdge--;

    const int full_width = (int) rightEdge - (int) leftEdge + 1;

    // If they supplied a buffer and it's not big enough, tell them
    if( filter->coeff && filter->width < full_width ) {
        filter->width = full_width;
        filter->center = -1;
        return;
    }

    filter->width = full_width;
    filter->center = - (int) leftEdge;

    if( !filter->coeff )
        filter->coeff = g_slice_alloc( sizeof(float) * filter->width );

    float sum = 0.0f;

    for( int i = 0; i < filter->width; i++ ) {
        filter->coeff[i] = 1.0f - fabsf( (1.0f / width) * ((i - filter->center) - offset) );
        sum += filter->coeff[i];
    }

    if( sub < 1.0f && sum != 0.0f ) {
        // Normalize to unity in the passband
        for( int i = 0; i < filter->width; i++ ) {
            filter->coeff[i] /= sum;
        }
    }
}

EXPORT void
filter_createLanczos( float sub, int kernel_size, float offset, fir_filter *filter ) {
    g_assert(filter);
    g_assert(sub > 0.0f);
    g_assert(kernel_size > 0);

    const bool down = sub < 1.0f;
    const float width = down ? (1.0f / sub) : sub;

    float leftEdge = ceilf(offset - kernel_size * width);
    float rightEdge = floorf(offset + kernel_size * width);

    if( G_UNLIKELY(leftEdge == offset - kernel_size * width) )
        leftEdge++;

    if( G_UNLIKELY(rightEdge == offset + kernel_size * width) )
        rightEdge--;

    const int full_width = (int) rightEdge - (int) leftEdge + 1;

    // If they supplied a buffer and it's not big enough, tell them
    if( filter->coeff && filter->width < full_width ) {
        filter->width = full_width;
        filter->center = -1;
        return;
    }

    filter->width = full_width;
    filter->center = - (int) leftEdge;

    if( !filter->coeff )
        filter->coeff = g_slice_alloc( sizeof(float) * filter->width );

    float sum = 0.0f;

    for( int i = 0; i < filter->width; i++ ) {
        double x = (1.0 / width) * ((i - filter->center) - (double) offset);

        if( G_UNLIKELY(x == 0.0) ) {
            filter->coeff[i] = 1.0f;
        }
        else if( G_UNLIKELY(x <= -kernel_size || x >= kernel_size) ) {
            filter->coeff[i] = 0.0f;
        }
        else {
            double num = kernel_size * sin(G_PI * x) * sin(G_PI * x / kernel_size);
            double den = G_PI * G_PI * x * x;

            double result = num / den;

            if( G_LIKELY(isfinite( result )) )
                filter->coeff[i] = (float) result;
            else
                filter->coeff[i] = 1.0f;
        }

        sum += filter->coeff[i];
    }

    if( sub < 1.0f && sum != 0.0f ) {
        // Normalize to unity in the passband
        for( int i = 0; i < filter->width; i++ ) {
            filter->coeff[i] /= sum;
        }
    }

/*    for( int i = 0; i < filter->width; i++ ) {
        g_print( "%f <- %f %s\n", filter->coeff[i], (float)((1.0 / width) * ((i - filter->center) - (double) offset)),
            i == filter->center ? "<- center" : "" );
    }*/
}

EXPORT void
filter_free( fir_filter *filter ) {
    g_slice_free1( filter->width * sizeof(float), filter->coeff );
}

