
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

    if( down ) {
        float sum = 0.0f;

        for( int i = 0; i < filter->width; i++ ) {
            filter->coeff[i] = (1.0f - sub * fabsf((i - filter->center) - offset));
            sum += filter->coeff[i];
        }

        for( int i = 0; i < filter->width; i++ ) {
            filter->coeff[i] /= sum;
        }
    }
    else {
        for( int i = 0; i < filter->width; i++ )
            filter->coeff[i] = 1.0f - (1.0f / sub) * fabsf((i - filter->center) - offset);
    }
}

void
filter_free( fir_filter *filter ) {
    g_slice_free1( filter->width * sizeof(float), filter->coeff );
}

