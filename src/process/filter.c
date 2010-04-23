
#include <glib.h>
#include <math.h>
#include "filter.h"

void
filter_createTriangle( float sub, float offset, fir_filter *filter ) {
    // The filter we're coming up with is y(x) = 1 - (1/sub) * abs(x - offset)
    // Goes to zero at x = offset +/- sub

    float leftEdge = ceilf(offset - sub);
    float rightEdge = floorf(offset + sub);

    if( leftEdge == offset - sub )
        leftEdge++;

    if( rightEdge == offset + sub )
        rightEdge--;

    int full_width = (int) rightEdge - (int) leftEdge + 1;

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

    for( int i = 0; i < filter->width; i++ )
        filter->coeff[i] = 1.0f - (1.0f / sub) * fabsf((i - filter->center) - offset);
}

void
filter_free( fir_filter *filter ) {
    g_slice_free1( filter->width * sizeof(float), filter->coeff );
}

