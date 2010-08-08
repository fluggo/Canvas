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

#include <string.h>
#include <math.h>
#include "video_mix.h"
#include "video_scale.h"
#include "filter.h"

static void
video_fill_zero_f32( rgba_frame_f32 *target ) {
    // You know what? Skip the pleasantries:
    v2i size;
    box2i_getSize( &target->fullDataWindow, &size );

    memset( target->frameData, 0, size.x * size.y * sizeof(rgba_f32) );
}

static void
video_scale_bilinear_vertical_f32( rgba_frame_f32 *target, float tymin, rgba_frame_f32 *source, float symin, float factor ) {
    box2i srect = source->currentDataWindow, trect = target->fullDataWindow;

    int xmin = max( source->currentDataWindow.min.x, target->fullDataWindow.min.x );
    int xmax = min( source->currentDataWindow.max.x, target->fullDataWindow.max.x );

    // These will hold how much of the target frame we actually used
    int ymin = G_MAXINT, ymax = G_MININT;

    video_fill_zero_f32( target );

    if( factor == 1.0f && tymin == symin ) {
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
    if( factor > 1.0f ) {
        for( int sy = srect.min.y; sy <= srect.max.y; sy++ ) {
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
    }
    else {
        for( int ty = trect.min.y; ty <= trect.max.y; ty++ ) {
            rgba_f32 *trow = getPixel_f32( target, xmin, ty );

            float source_center_f = (ty - tymin) / factor + symin;
            int source_center = (int) floor( source_center_f );

            filter.width = filter_width;
            filter_createTriangle( factor, source_center_f - source_center, &filter );

            for( int fy = 0; fy < filter.width; fy++ ) {
                int sy = source_center - filter.center + fy;

                if( sy < srect.min.y || sy > srect.max.y )
                    continue;

                rgba_f32 *srow = getPixel_f32( source, xmin, sy );

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
    }

    box2i_set( &target->currentDataWindow, xmin, ymin, xmax, ymax );

    g_slice_free1( sizeof(float) * filter_width, filter.coeff );
}

static void
video_scale_bilinear_horizontal_f32( rgba_frame_f32 *target, float txmin, rgba_frame_f32 *source, float sxmin, float factor ) {
    // BJC: This is the more-or-less direct translation of vertical, which means
    // it has somewhat poor locality of reference

    box2i srect = source->currentDataWindow, trect = target->fullDataWindow;

    int ymin = max( source->currentDataWindow.min.y, target->fullDataWindow.min.y );
    int ymax = min( source->currentDataWindow.max.y, target->fullDataWindow.max.y );

    // These will hold how much of the target frame we actually used
    int xmin = G_MAXINT, xmax = G_MININT;

    video_fill_zero_f32( target );

    if( factor == 1.0f && txmin == sxmin ) {
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
    if( factor > 1.0f ) {
        for( int sx = srect.min.x; sx <= srect.max.x; sx++ ) {
            float target_center_f = (sx - sxmin) * factor + txmin;
            int target_center = (int) floor( target_center_f );

            filter.width = filter_width;
            filter_createTriangle( factor, target_center_f - target_center, &filter );

            for( int y = ymin; y <= ymax; y++ ) {
                rgba_f32 *s = getPixel_f32( source, sx, y );
                rgba_f32 *t = getPixel_f32( target, target_center - filter.center, y );

                for( int fx = 0; fx < filter.width; fx++ ) {
                    // BJC: Also, this is one naaaasty inner loop, so this will require some work
                    int tx = target_center - filter.center + fx;

                    if( tx < trect.min.x || tx > trect.max.x )
                        continue;

                    t[fx].r += s->r * filter.coeff[fx];
                    t[fx].g += s->g * filter.coeff[fx];
                    t[fx].b += s->b * filter.coeff[fx];
                    t[fx].a += s->a * filter.coeff[fx];

                    xmin = min( xmin, tx );
                    xmax = max( xmax, tx );
                }
            }
        }

        box2i_set( &target->currentDataWindow, xmin, ymin, xmax, ymax );
    }
    else {
        for( int tx = trect.min.x; tx <= trect.max.x; tx++ ) {
            float source_center_f = (tx - txmin) / factor + sxmin;
            int source_center = (int) floor( source_center_f );

            filter.width = filter_width;
            filter_createTriangle( factor, source_center_f - source_center, &filter );

            // TODO: We can skip the inner loop if the filter wouldn't touch any of the source pixels

            for( int y = ymin; y <= ymax; y++ ) {
                rgba_f32 *s = getPixel_f32( source, source_center - filter.center, y );
                rgba_f32 *t = getPixel_f32( target, tx, y );

                for( int fx = 0; fx < filter.width; fx++ ) {
                    // BJC: Also, this is one naaaasty inner loop, so this will require some work
                    int sx = source_center - filter.center + fx;

                    if( sx < srect.min.x || sx > srect.max.x )
                        continue;

                    t[0].r += s[fx].r * filter.coeff[fx];
                    t[0].g += s[fx].g * filter.coeff[fx];
                    t[0].b += s[fx].b * filter.coeff[fx];
                    t[0].a += s[fx].a * filter.coeff[fx];

                    xmin = min( xmin, tx );
                    xmax = max( xmax, tx );
                }
            }
        }

        box2i_set( &target->currentDataWindow, xmin, ymin, xmax, ymax );
    }

    g_slice_free1( sizeof(float) * filter_width, filter.coeff );
}

EXPORT void
video_scale_bilinear_f32( rgba_frame_f32 *target, v2f target_point, rgba_frame_f32 *source, v2f source_point, v2f factors ) {
    if( factors.x == 1.0f && target_point.x == source_point.x ) {
        if( factors.y == 1.0f && target_point.y == source_point.y ) {
            video_copy_frame_alpha_f32( target, source, 1.0f );
            return;
        }

        video_scale_bilinear_vertical_f32( target, target_point.y, source, source_point.y, factors.y );
        return;
    }
    else if( factors.y == 1.0f && target_point.y == source_point.y ) {
        video_scale_bilinear_horizontal_f32( target, target_point.x, source, source_point.x, factors.x );
        return;
    }

    // We need another temp frame here; we'll perform the scale in the direction with the smallest scale factor first,
    // both to reduce the amount of memory we need and reduce the computation time for the second half of the scale
    rgba_frame_f32 temp_frame;
    v2i size;

    if( factors.x < factors.y ) {
        box2i_set( &temp_frame.fullDataWindow,
            (int)(source_point.x - (target_point.x - target->fullDataWindow.min.x) * factors.x),
            source->currentDataWindow.min.y,
            (int)(source_point.x + (target->fullDataWindow.max.x - target_point.x) * factors.x),
            source->currentDataWindow.max.y );

        box2i_intersect( &temp_frame.fullDataWindow, &temp_frame.fullDataWindow, &target->fullDataWindow );
        box2i_getSize( &temp_frame.fullDataWindow, &size );

        temp_frame.frameData = g_slice_alloc( sizeof(rgba_f32) * size.y * size.x );
        temp_frame.stride = size.x;

        video_scale_bilinear_horizontal_f32( &temp_frame, target_point.x, source, source_point.x, factors.x );
        video_scale_bilinear_vertical_f32( target, target_point.y, &temp_frame, source_point.y, factors.y );

        g_slice_free1( sizeof(rgba_f32) * size.y * size.x, temp_frame.frameData );
    }
    else {
        box2i_set( &temp_frame.fullDataWindow,
            source->currentDataWindow.min.x,
            (int)(source_point.y - (target_point.y - target->fullDataWindow.min.y) * factors.y),
            source->currentDataWindow.max.x,
            (int)(source_point.y + (target->fullDataWindow.max.y - target_point.y) * factors.y) );

        box2i_intersect( &temp_frame.fullDataWindow, &temp_frame.fullDataWindow, &target->fullDataWindow );
        box2i_getSize( &temp_frame.fullDataWindow, &size );

        temp_frame.frameData = g_slice_alloc( sizeof(rgba_f32) * size.y * size.x );
        temp_frame.stride = size.x;

        video_scale_bilinear_vertical_f32( &temp_frame, target_point.y, source, source_point.y, factors.y );
        video_scale_bilinear_horizontal_f32( target, target_point.x, &temp_frame, source_point.x, factors.x );

        g_slice_free1( sizeof(rgba_f32) * size.y * size.x, temp_frame.frameData );
    }
}

EXPORT void
video_scale_bilinear_f32_pull( rgba_frame_f32 *target, v2f target_point, video_source *source, int frame, box2i *source_rect, v2f source_point, v2f factors ) {
    if( factors.x == 0.0f || factors.y == 0.0f ) {
        box2i_setEmpty( &target->currentDataWindow );
        return;
    }

    if( factors.x == 1.0f && factors.y == 1.0f && target_point.x == source_point.x && target_point.y == source_point.y ) {
        video_getFrame_f32( source, frame, target );
        return;
    }

    rgba_frame_f32 temp_frame;
    v2i size;

    box2i_set( &temp_frame.fullDataWindow,
        (int)(source_point.x - (target_point.x - target->fullDataWindow.min.x) / factors.x),
        (int)(source_point.y - (target_point.y - target->fullDataWindow.min.y) / factors.y),
        (int)(source_point.x + (target->fullDataWindow.max.x - target_point.x) / factors.x),
        (int)(source_point.y + (target->fullDataWindow.max.y - target_point.y) / factors.y) );

    box2i_intersect( &temp_frame.fullDataWindow, &temp_frame.fullDataWindow, source_rect );

    box2i_getSize( &temp_frame.fullDataWindow, &size );

    temp_frame.frameData = g_slice_alloc( sizeof(rgba_f32) * size.y * size.x );
    temp_frame.stride = size.x;

    video_getFrame_f32( source, frame, &temp_frame );
    video_scale_bilinear_f32( target, target_point, &temp_frame, source_point, factors );

    g_slice_free1( sizeof(rgba_f32) * size.y * size.x, temp_frame.frameData );
}

