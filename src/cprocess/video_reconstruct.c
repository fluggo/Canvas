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

typedef struct {
    float cb, cr;
} cbcr_f32;

float
studio_chroma8_to_float( uint8_t chroma ) {
    return (chroma - 128.0f) / 224.0f;
}

float
studio_luma8_to_float( uint8_t luma ) {
    return (luma - 16.0f) / 219.0f;
}

/*
    Function: video_reconstruct_dv
    Reconstructs planar standard-definition NTSC DV:

    720x480 YCbCr
    4:1:1 subsampling, co-sited with left pixel
    Rec 709 matrix
    Rec 709 transfer function
*/
EXPORT void
video_reconstruct_dv( coded_image *planar, rgba_frame_f16 *frame ) {
    const int full_width = 720, full_height = 480;

    // Rec. 601 YCbCr->RGB matrix in Poynton, p. 305:
/*    const float colorMatrix[3][3] = {
        { 1.0f,  0.0f,       1.402f },
        { 1.0f, -0.344136f, -0.714136f },
        { 1.0f,  1.772f,     0.0f }
    };*/

    // Rec. 709 YCbCr->RGB matrix in Poynton, p. 316:
    const float colorMatrix[3][3] = {
        { 1.0f,  0.0f,       1.5748f },
        { 1.0f, -0.187324f, -0.468124f },
        { 1.0f,  1.8556f,    0.0f }
    };

    // Offset the frame so that line zero is part of the first field
    v2i picOffset = { 0, -1 };

    // Set up the current window
    box2i_set( &frame->current_window,
        max( picOffset.x, frame->full_window.min.x ),
        max( picOffset.y, frame->full_window.min.y ),
        min( full_width + picOffset.x - 1, frame->full_window.max.x ),
        min( full_height + picOffset.y - 1, frame->full_window.max.y ) );

    // Set up subsample support
    const int subX = 4;
    const float subOffsetX = 0.0f;

    // BJC: What follows is the horizontal-subsample-only case
    fir_filter triangleFilter = { NULL };
    filter_createTriangle( subX, subOffsetX, &triangleFilter );

    // Temp rows aligned to the AVFrame buffer [0, width)
    rgba_f32 *tempRow = g_slice_alloc( sizeof(rgba_f32) * full_width );
    cbcr_f32 *tempChroma = g_slice_alloc( sizeof(cbcr_f32) * full_width );

    // Turn into half RGB
    for( int row = frame->current_window.min.y - picOffset.y; row <= frame->current_window.max.y - picOffset.y; row++ ) {
        uint8_t *yrow = (uint8_t*) planar->data[0] + (row * planar->stride[0]);
        uint8_t *cbrow = (uint8_t*) planar->data[1] + (row * planar->stride[1]);
        uint8_t *crrow = (uint8_t*) planar->data[2] + (row * planar->stride[2]);

        memset( tempChroma, 0, sizeof(cbcr_f32) * full_width );

        int startx = 0, endx = (full_width - 1) / subX;

        for( int x = startx; x <= endx; x++ ) {
            float cb = studio_chroma8_to_float( cbrow[x] ), cr = studio_chroma8_to_float( crrow[x] );

            for( int i = max(frame->current_window.min.x - picOffset.x, x * subX - triangleFilter.center );
                    i <= min(frame->current_window.max.x - picOffset.x, x * subX + (triangleFilter.width - triangleFilter.center - 1)); i++ ) {

                tempChroma[i].cb += cb * triangleFilter.coeff[i - x * subX + triangleFilter.center];
                tempChroma[i].cr += cr * triangleFilter.coeff[i - x * subX + triangleFilter.center];
            }
        }

        for( int x = frame->current_window.min.x; x <= frame->current_window.max.x; x++ ) {
            float y = studio_luma8_to_float( yrow[x - picOffset.x] );

            tempRow[x].r = y * colorMatrix[0][0] +
                tempChroma[x - picOffset.x].cb * colorMatrix[0][1] +
                tempChroma[x - picOffset.x].cr * colorMatrix[0][2];
            tempRow[x].g = y * colorMatrix[1][0] +
                tempChroma[x - picOffset.x].cb * colorMatrix[1][1] +
                tempChroma[x - picOffset.x].cr * colorMatrix[1][2];
            tempRow[x].b = y * colorMatrix[2][0] +
                tempChroma[x - picOffset.x].cb * colorMatrix[2][1] +
                tempChroma[x - picOffset.x].cr * colorMatrix[2][2];
            tempRow[x].a = 1.0f;
        }

        rgba_f16 *out = video_get_pixel_f16( frame, frame->current_window.min.x, row + picOffset.y );

        rgba_f32_to_f16( out, tempRow + frame->current_window.min.x - picOffset.x,
            frame->current_window.max.x - frame->current_window.min.x + 1 );
        video_transfer_rec709_to_linear_scene( &out->r, &out->r,
            (sizeof(rgba_f16) / sizeof(half)) * (frame->current_window.max.x - frame->current_window.min.x + 1) );
    }

    filter_free( &triangleFilter );
    g_slice_free1( sizeof(rgba_f32) * full_width, tempRow );
    g_slice_free1( sizeof(cbcr_f32) * full_width, tempChroma );
}

