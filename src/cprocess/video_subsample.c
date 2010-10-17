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

static void
free_coded_image( coded_image *image ) {
    g_assert(image);

    for( int i = 0; i < CODED_IMAGE_MAX_PLANES; i++ ) {
        if( image->data[i] )
            g_slice_free1( image->stride[i] * image->line_count[i], image->data[i] );
    }

    g_slice_free( coded_image, image );
}

static coded_image *
coded_image_alloc_impl( const int *strides, const int *line_counts, int count, bool zero ) {
    g_assert(strides);
    g_assert(line_counts);
    g_assert(count <= CODED_IMAGE_MAX_PLANES);

    coded_image *result = g_slice_new0( coded_image );

    for( int i = 0; i < count; i++ ) {
        g_assert(strides[i] >= 0);
        g_assert(line_counts[i] >= 0);

        result->stride[i] = strides[i];
        result->line_count[i] = line_counts[i];

        if( strides[i] == 0 || line_counts[i] == 0 )
            continue;

        if( zero )
            result->data[i] = g_slice_alloc0( strides[i] * line_counts[i] );
        else
            result->data[i] = g_slice_alloc( strides[i] * line_counts[i] );
    }

    result->free_func = (GFreeFunc) free_coded_image;

    return result;
}

EXPORT coded_image *
coded_image_alloc( const int *strides, const int *line_counts, int count ) {
    return coded_image_alloc_impl( strides, line_counts, count, false );
}

EXPORT coded_image *
coded_image_alloc0( const int *strides, const int *line_counts, int count ) {
    return coded_image_alloc_impl( strides, line_counts, count, true );
}


typedef struct {
    float cb, cr;
} cbcr_f32;

float
studio_float_to_chroma8( float chroma ) {
    return chroma * 224.0f + 128.0f;
}

float
studio_float_to_luma8( float luma ) {
    return luma * 219.0f + 16.0f;
}

/*
    Function: video_subsample_dv
    Subsamples to planar standard-definition NTSC DV:

    720x480 YCbCr
    4:1:1 subsampling, co-sited with left pixel
    Rec 709 matrix
    Rec 709 transfer function
*/
EXPORT coded_image *
video_subsample_dv( rgba_frame_f16 *frame ) {
    const int full_width = 720, full_height = 480;

    // RGB->Rec. 709 YPbPr matrix in Poynton, p. 315:
    const float colorMatrix[3][3] = {
        {  0.2126f,    0.7152f,    0.0722f   },
        { -0.114572f, -0.385428f,  0.5f      },
        {  0.5f,      -0.454153f, -0.045847f }
    };

    // Offset the frame so that line zero is part of the first field
    const v2i picOffset = { 0, -1 };

    // Set up subsample support
    const int subX = 4;
    const float subOffsetX = 0.0f;

    const int strides[3] = { full_width, full_width / subX, full_width / subX };
    const int line_counts[3] = { full_height, full_height, full_height };

    // Set up the current window
    box2i window = {
        { max( picOffset.x, frame->current_window.min.x ),
          max( picOffset.y, frame->current_window.min.y ) },
        { min( full_width + picOffset.x - 1, frame->current_window.max.x ),
          min( full_height + picOffset.y - 1, frame->current_window.max.y ) }
    };
    int window_width = window.max.x - window.min.x + 1;

    coded_image *planar = coded_image_alloc0( strides, line_counts, 3 );

    // BJC: What follows is the horizontal-subsample-only case
    fir_filter triangleFilter = { NULL };
    filter_createTriangle( 1.0f / (float) subX, subOffsetX, &triangleFilter );

    // Temp rows aligned to the input window [window.min.x, window.max.x]
    rgba_f32 *tempRow = g_slice_alloc( sizeof(rgba_f32) * window_width );
    cbcr_f32 *tempChroma = g_slice_alloc( sizeof(cbcr_f32) * window_width );

    // Turn into half RGB
    for( int row = window.min.y - picOffset.y; row <= window.max.y - picOffset.y; row++ ) {
        uint8_t *yrow = (uint8_t*) planar->data[0] + (row * planar->stride[0]);
        uint8_t *cbrow = (uint8_t*) planar->data[1] + (row * planar->stride[1]);
        uint8_t *crrow = (uint8_t*) planar->data[2] + (row * planar->stride[2]);

        rgba_f16 *in = video_get_pixel_f16( frame, window.min.x, row + picOffset.y );

        video_transfer_linear_to_rec709( &in->r, &in->r, (sizeof(rgba_f16) / sizeof(half)) * window_width );
        rgba_f16_to_f32( tempRow, in, window_width );

        for( int x = 0; x < window_width; x++ ) {
            float y = studio_float_to_luma8(
                tempRow[x].r * colorMatrix[0][0] +
                tempRow[x].g * colorMatrix[0][1] +
                tempRow[x].b * colorMatrix[0][2] );
            tempChroma[x].cb =
                tempRow[x].r * colorMatrix[1][0] +
                tempRow[x].g * colorMatrix[1][1] +
                tempRow[x].b * colorMatrix[1][2];
            tempChroma[x].cr =
                tempRow[x].r * colorMatrix[2][0] +
                tempRow[x].g * colorMatrix[2][1] +
                tempRow[x].b * colorMatrix[2][2];

            yrow[x + window.min.x] = (uint8_t) y;
        }

        for( int tx = window.min.x / subX; tx <= window.max.x / subX; tx++ ) {
            float cb = 0.0f, cr = 0.0f;

            for( int sx = max(window.min.x, tx * subX - triangleFilter.center);
                sx <= min(window.max.x, tx * subX + (triangleFilter.width - triangleFilter.center - 1)); sx++ ) {

                cb += tempChroma[sx - window.min.x].cb * triangleFilter.coeff[sx - tx * subX + triangleFilter.center];
                cr += tempChroma[sx - window.min.x].cr * triangleFilter.coeff[sx - tx * subX + triangleFilter.center];
            }

            cbrow[tx] = (uint8_t) studio_float_to_chroma8( cb );
            crrow[tx] = (uint8_t) studio_float_to_chroma8( cr );
        }
    }

    filter_free( &triangleFilter );
    g_slice_free1( sizeof(rgba_f32) * window_width, tempRow );
    g_slice_free1( sizeof(cbcr_f32) * window_width, tempChroma );

    return planar;
}

