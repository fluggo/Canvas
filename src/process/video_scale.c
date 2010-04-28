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

#include "video_mix.h"
#include "video_scale.h"
#include "filter.h"

static void
video_fill_zero_f32( rgba_f32_frame *target ) {
    // You know what? Skip the pleasantries:
    v2i size;
    box2i_getSize( &target->fullDataWindow, &size );

    memset( target->frameData, 0, size.x * size.y * sizeof(rgba_f32) );
}

static void
video_scale_bilinear_vertical_f32( rgba_f32_frame *target, float tymin, rgba_f32_frame *source, float symin, float factor ) {
    box2i srect = source->currentDataWindow, trect = target->fullDataWindow;

    int xmin = max( source->currentDataWindow.min.x, target->fullDataWindow.min.x );
    int xmax = min( source->currentDataWindow.max.x, target->fullDataWindow.max.x );

    // These will hold how much of the target frame we actually used
    int ymin = G_MAXINT, ymax = G_MININT;

    video_fill_zero_f32( target );

    if( factor == 1.0f ) {
        video_copy_frame_alpha_f32( target, source, 1.0f );
        return;
    }

    // Determine an appropriate filter size
    fir_filter filter = { .coeff = &factor, .width = 0 };

    filter_createTriangle( factor, 0.0f, &filter );

    int filter_width = filter.width + 3;
    filter.coeff = g_slice_alloc( sizeof(float) * filter_width );

    // General case (offset can be different on each row, so we have to create the filter multiple times)

    // BJC: I'm going to go for the case where I only have to create one filter at a time, and that's
    // by zeroing out the output and going at it one input-row at a time
    for( int sy = srect.min.y; sy < srect.max.y; sy++ ) {
        rgba_f32 *srow = getPixel_f32( source, xmin, sy );

        float target_center_f = (sy - symin) * factor + tymin;
        int target_center = (int) floor( target_center_f );

        filter.width = filter_width;
        filter_createTriangle( factor, target_center_f - target_center, &filter );

        for( int fy = 0; fy < filter.width; fy++ ) {
            int ty = target_center - filter.center + fy;

            if( ty < trect.min.y || ty > trect.max.y )
                continue;

            rgba_f32 *trow = getPixel_f32( target, xmin, ty );

            for( int x = 0; x < (xmax - xmin + 1); x++ ) {
                trow[x].r += srow[x].r * filter.coeff[fy];
                trow[x].g += srow[x].g * filter.coeff[fy];
                trow[x].b += srow[x].b * filter.coeff[fy];
                trow[x].a += srow[x].a * filter.coeff[fy];
            }

            ymin = min( ymin, ty );
            ymax = max( ymax, ty );
        }
    }

    box2i_set( &target->currentDataWindow, xmin, ymin, xmax, ymax );

    g_slice_free1( sizeof(float) * filter_width, filter.coeff );
}

EXPORT void
video_scale_bilinear_f32( rgba_f32_frame *target, v2f target_point, rgba_f32_frame *source, v2f source_point, v2f factors ) {
    video_scale_bilinear_vertical_f32( target, target_point.y, source, source_point.y, factors.y );
}

