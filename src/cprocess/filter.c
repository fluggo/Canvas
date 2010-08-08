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

#include <glib.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>
#include "filter.h"

void
filter_createTriangle( float sub, float offset, fir_filter *filter ) {
    // sub = f'/f where f is the original sampling rate (you can assume 1) and f' is the new sampling rate
    // sub = 4/1 is upsampling by a factor of four, sub = 1/4 is downsampling by the same

    // For upsampling, the filter we're coming up with is y(x) = 1 - (1/sub) * abs(x - offset)
    // For downsampling (sub < 1), it's y(x) = 1.0f - sub * abs(x - offset) scaled to sum to unity
    // Upsample goes to zero at x = offset +/- sub, down at x = offset +- (1/sub)

    assert(filter);
    assert(sub > 0.0f);

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

void
filter_free( fir_filter *filter ) {
    g_slice_free1( filter->width * sizeof(float), filter->coeff );
}

