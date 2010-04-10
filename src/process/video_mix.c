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

#include "video_mix.h"

EXPORT void
video_mix_cross_f32_pull( rgba_f32_frame *out, video_source *a, int frame_a, video_source *b, int frame_b, float mix_b ) {
    mix_b = clampf(mix_b, 0.0f, 1.0f);

    if( mix_b == 0.0 ) {
        getFrame_f32( a, frame_a, out );
    }
    else if( mix_b == 1.0 ) {
        getFrame_f32( b, frame_b, out );
    }
    else {
        rgba_f32_frame tempFrame;
        v2i size;

        box2i_getSize( &out->fullDataWindow, &size );

        tempFrame.frameData = slice_alloc( sizeof(rgba_f32) * size.y * size.x );
        tempFrame.fullDataWindow = out->fullDataWindow;
        tempFrame.stride = size.x;

        getFrame_f32( a, frame_a, out );
        getFrame_f32( b, frame_b, &tempFrame );
        video_mix_cross_f32( out, out, &tempFrame, mix_b );

        slice_free( sizeof(rgba_f32) * size.y * size.x, tempFrame.frameData );
    }
}

EXPORT void
video_copy_frame_alpha_f32( rgba_f32_frame *out, rgba_f32_frame *in, float alpha ) {
    alpha = clampf(alpha, 0.0f, 1.0f);

    if( out == in && alpha == 1.0f )
        return;

    if( alpha == 0.0f ) {
        box2i_setEmpty( &out->currentDataWindow );
        return;
    }

    box2i inner;
    box2i_intersect( &inner, &out->fullDataWindow, &in->currentDataWindow );
    out->currentDataWindow = inner;

    bool is_empty = box2i_isEmpty( &inner );

    if( is_empty )
        return;

    int width = inner.max.x - inner.min.x + 1;

    for( int y = inner.min.y; y <= inner.max.y; y++ ) {
        rgba_f32 *row_out = getPixel_f32( out, inner.min.x, y );
        rgba_f32 *row_in = getPixel_f32( in, inner.min.x, y );

        memcpy( row_out, row_in, sizeof(rgba_f32) * width );

        if( alpha != 1.0f ) {
            for( int x = 0; x < width; x++ )
                row_out[x].a *= alpha;
        }
    }
}

EXPORT void
video_mix_cross_f32( rgba_f32_frame *out, rgba_f32_frame *a, rgba_f32_frame *b, float mix_b ) {
    box2i *awin = &a->currentDataWindow, *bwin = &b->currentDataWindow;

    mix_b = clampf(mix_b, 0.0f, 1.0f);
    const float mix_a = (1.0f - mix_b);

    if( box2i_isEmpty( awin ) ) {
        video_copy_frame_alpha_f32( out, b, mix_b );
        return;
    }
    else if( box2i_isEmpty( bwin ) ) {
        video_copy_frame_alpha_f32( out, a, mix_a );
        return;
    }

    box2i outer, inner;

    box2i_union( &outer, awin, bwin );
    box2i_intersect( &outer, &outer, &out->fullDataWindow );

    box2i_intersect( &inner, awin, bwin );
    box2i_intersect( &inner, &inner, &out->fullDataWindow );

    bool empty_inner_x = inner.min.x > inner.max.x,
        empty_inner_y = inner.min.y > inner.max.y;
    box2i_normalize( &inner );

    rgba_f32_frame *top = (awin->min.y < bwin->min.y) ? a : b,
        *bottom = (awin->max.y > bwin->max.y) ? a : b,
        *left = (awin->min.x < bwin->min.y) ? a : b,
        *right = (awin->max.x > bwin->max.x) ? a : b;

    const rgba_f32 zero = { 0.0, 0.0, 0.0, 0.0 };

    // Top: one frame or the other is up here
    for( int y = outer.min.y; y < inner.min.y; y++ ) {
        rgba_f32 *row_out = getPixel_f32( out, 0, y );
        rgba_f32 *row_top = getPixel_f32( top, 0, y );
        const float mix = (top == a) ? mix_a : mix_b;

        for( int x = outer.min.x; x < top->currentDataWindow.min.x; x++ )
            row_out[x] = zero;

        for( int x = top->currentDataWindow.min.x; x <= top->currentDataWindow.max.x; x++ ) {
            row_out[x] = row_top[x];
            row_out[x].a *= mix;
        }

        for( int x = top->currentDataWindow.max.x + 1; x <= outer.max.x; x++ )
            row_out[x] = zero;
    }

    // Middle
    if( empty_inner_y ) {
        // Neither frame appears here
        for( int y = inner.min.y; y <= inner.max.y; y++ ) {
            rgba_f32 *row_out = getPixel_f32( out, 0, y );

            for( int x = inner.min.x; x <= inner.max.x; x++ )
                row_out[x] = zero;
        }
    }
    else {
        // Both frames appear and might (or might not!) intersect
        const float mix_left = (left == a) ? mix_a : mix_b,
            mix_right = (right == a) ? mix_a : mix_b;

        for( int y = inner.min.y; y <= inner.max.y; y++ ) {
            rgba_f32 *row_out = getPixel_f32( out, 0, y );
            rgba_f32 *row_a = getPixel_f32( a, 0, y );
            rgba_f32 *row_b = getPixel_f32( b, 0, y );
            rgba_f32 *row_left = (left == a) ? row_a : row_b,
                *row_right = (right == a) ? row_a : row_b;

            for( int x = outer.min.x; x < inner.min.x; x++ ) {
                row_out[x] = row_left[x];
                row_out[x].a *= mix_left;
            }

            if( empty_inner_x ) {
                for( int x = inner.min.x; x <= inner.max.x; x++ )
                    row_out[x] = zero;
            }
            else {
                for( int x = inner.min.x; x <= inner.max.x; x++ ) {
                    float alpha_a = row_a[x].a * mix_a;
                    float alpha_b = row_b[x].a * mix_b;

                    row_out[x].a = alpha_a + alpha_b;

                    if( row_out[x].a != 0.0 ) {
                        row_out[x].r = (row_a[x].r * alpha_a + row_b[x].r * alpha_b) / row_out[x].a;
                        row_out[x].g = (row_a[x].g * alpha_a + row_b[x].g * alpha_b) / row_out[x].a;
                        row_out[x].b = (row_a[x].b * alpha_a + row_b[x].b * alpha_b) / row_out[x].a;
                    }
                    else {
                        row_out[x] = zero;
                    }
                }
            }

            for( int x = inner.max.x + 1; x <= outer.max.x; x++ ) {
                row_out[x] = row_right[x];
                row_out[x].a *= mix_right;
            }
        }
    }

    // Bottom: one frame or the other is down here
    for( int y = inner.max.y + 1; y <= outer.max.y; y++ ) {
        rgba_f32 *row_out = getPixel_f32( out, 0, y );
        rgba_f32 *row_bottom = getPixel_f32( bottom, 0, y );
        const float mix = (bottom == a) ? mix_a : mix_b;

        for( int x = outer.min.x; x < bottom->currentDataWindow.min.x; x++ )
            row_out[x] = zero;

        for( int x = bottom->currentDataWindow.min.x; x <= bottom->currentDataWindow.max.x; x++ ) {
            row_out[x] = row_bottom[x];
            row_out[x].a *= mix;
        }

        for( int x = bottom->currentDataWindow.max.x + 1; x <= outer.max.x; x++ )
            row_out[x] = zero;
    }

    out->currentDataWindow = outer;
}

EXPORT void
video_mix_over_f32( rgba_f32_frame *out, rgba_f32_frame *a, rgba_f32_frame *b, float mix_a, float mix_b ) {
    box2i *awin = &a->currentDataWindow, *bwin = &b->currentDataWindow;

    mix_a = clampf(mix_a, 0.0f, 1.0f);
    mix_b = clampf(mix_b, 0.0f, 1.0f);

    if( box2i_isEmpty( awin ) || mix_a == 0.0f ) {
        video_copy_frame_alpha_f32( out, b, mix_b );
        return;
    }
    else if( box2i_isEmpty( bwin ) || mix_b == 0.0f ) {
        video_copy_frame_alpha_f32( out, a, mix_a );
        return;
    }

    box2i outer, inner;

    box2i_union( &outer, awin, bwin );
    box2i_intersect( &outer, &outer, &out->fullDataWindow );

    box2i_intersect( &inner, awin, bwin );
    box2i_intersect( &inner, &inner, &out->fullDataWindow );

    bool empty_inner_x = inner.min.x > inner.max.x,
        empty_inner_y = inner.min.y > inner.max.y;
    box2i_normalize( &inner );

    rgba_f32_frame *top = (awin->min.y < bwin->min.y) ? a : b,
        *bottom = (awin->max.y > bwin->max.y) ? a : b,
        *left = (awin->min.x < bwin->min.y) ? a : b,
        *right = (awin->max.x > bwin->max.x) ? a : b;

    const rgba_f32 zero = { 0.0, 0.0, 0.0, 0.0 };

    // Top: one frame or the other is up here
    for( int y = outer.min.y; y < inner.min.y; y++ ) {
        rgba_f32 *row_out = getPixel_f32( out, 0, y );
        rgba_f32 *row_top = getPixel_f32( top, 0, y );
        const float mix = (top == a) ? mix_a : mix_b;

        for( int x = outer.min.x; x < top->currentDataWindow.min.x; x++ )
            row_out[x] = zero;

        for( int x = top->currentDataWindow.min.x; x <= top->currentDataWindow.max.x; x++ ) {
            row_out[x] = row_top[x];
            row_out[x].a *= mix;
        }

        for( int x = top->currentDataWindow.max.x + 1; x <= outer.max.x; x++ )
            row_out[x] = zero;
    }

    // Middle
    if( empty_inner_y ) {
        // Neither frame appears here
        for( int y = inner.min.y; y <= inner.max.y; y++ ) {
            rgba_f32 *row_out = getPixel_f32( out, 0, y );

            for( int x = inner.min.x; x <= inner.max.x; x++ )
                row_out[x] = zero;
        }
    }
    else {
        // Both frames appear and might (or might not!) intersect
        const float mix_left = (left == a) ? mix_a : mix_b,
            mix_right = (right == a) ? mix_a : mix_b;

        for( int y = inner.min.y; y <= inner.max.y; y++ ) {
            rgba_f32 *row_out = getPixel_f32( out, 0, y );
            rgba_f32 *row_a = getPixel_f32( a, 0, y );
            rgba_f32 *row_b = getPixel_f32( b, 0, y );
            rgba_f32 *row_left = (left == a) ? row_a : row_b,
                *row_right = (right == a) ? row_a : row_b;

            for( int x = outer.min.x; x < inner.min.x; x++ ) {
                row_out[x] = row_left[x];
                row_out[x].a *= mix_left;
            }

            if( empty_inner_x ) {
                for( int x = inner.min.x; x <= inner.max.x; x++ )
                    row_out[x] = zero;
            }
            else {
                for( int x = inner.min.x; x <= inner.max.x; x++ ) {
                    float alpha_b = row_b[x].a * mix_b;
                    float alpha_a = row_a[x].a * mix_a * (1.0f - row_b[x].a * mix_b);

                    row_out[x].a = alpha_a + alpha_b;

                    if( row_out[x].a != 0.0 ) {
                        row_out[x].r = (row_a[x].r * alpha_a + row_b[x].r * alpha_b) / row_out[x].a;
                        row_out[x].g = (row_a[x].g * alpha_a + row_b[x].g * alpha_b) / row_out[x].a;
                        row_out[x].b = (row_a[x].b * alpha_a + row_b[x].b * alpha_b) / row_out[x].a;
                    }
                    else {
                        row_out[x] = zero;
                    }
                }
            }

            for( int x = inner.max.x + 1; x <= outer.max.x; x++ ) {
                row_out[x] = row_right[x];
                row_out[x].a *= mix_right;
            }
        }
    }

    // Bottom: one frame or the other is down here
    for( int y = inner.max.y + 1; y <= outer.max.y; y++ ) {
        rgba_f32 *row_out = getPixel_f32( out, 0, y );
        rgba_f32 *row_bottom = getPixel_f32( bottom, 0, y );
        const float mix = (bottom == a) ? mix_a : mix_b;

        for( int x = outer.min.x; x < bottom->currentDataWindow.min.x; x++ )
            row_out[x] = zero;

        for( int x = bottom->currentDataWindow.min.x; x <= bottom->currentDataWindow.max.x; x++ ) {
            row_out[x] = row_bottom[x];
            row_out[x].a *= mix;
        }

        for( int x = bottom->currentDataWindow.max.x + 1; x <= outer.max.x; x++ )
            row_out[x] = zero;
    }

    out->currentDataWindow = outer;
}
