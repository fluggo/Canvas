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

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "fluggo.media.cprocess.video_mix"

EXPORT void
video_copy_frame_f16( rgba_frame_f16 *out, rgba_frame_f16 *in ) {
    box2i inner;
    box2i_intersect( &inner, &out->full_window, &in->current_window );
    out->current_window = inner;

    if( box2i_is_empty( &inner ) )
        return;

    int width = inner.max.x - inner.min.x + 1;

    for( int y = inner.min.y; y <= inner.max.y; y++ ) {
        rgba_f16 *row_out = video_get_pixel_f16( out, inner.min.x, y );
        rgba_f16 *row_in = video_get_pixel_f16( in, inner.min.x, y );

        memcpy( row_out, row_in, sizeof(rgba_f16) * width );
    }
}

EXPORT void
video_mix_cross_f32_pull( rgba_frame_f32 *out, video_source *a, int frame_a, video_source *b, int frame_b, float mix_b ) {
    mix_b = clampf(mix_b, 0.0f, 1.0f);

    if( mix_b == 0.0 ) {
        video_get_frame_f32( a, frame_a, out );
    }
    else if( mix_b == 1.0 ) {
        video_get_frame_f32( b, frame_b, out );
    }
    else {
        rgba_frame_f32 tempFrame;
        v2i size;

        box2i_get_size( &out->full_window, &size );

        tempFrame.data = g_slice_alloc( sizeof(rgba_f32) * size.y * size.x );
        tempFrame.full_window = out->full_window;

        video_get_frame_f32( a, frame_a, out );
        video_get_frame_f32( b, frame_b, &tempFrame );
        video_mix_cross_f32( out, out, &tempFrame, mix_b );

        g_slice_free1( sizeof(rgba_f32) * size.y * size.x, tempFrame.data );
    }
}

EXPORT void
video_copy_frame_alpha_f32( rgba_frame_f32 *out, rgba_frame_f32 *in, float alpha ) {
    alpha = clampf(alpha, 0.0f, 1.0f);

    if( out == in && alpha == 1.0f )
        return;

    if( alpha == 0.0f ) {
        box2i_set_empty( &out->current_window );
        return;
    }

    box2i inner;
    box2i_intersect( &inner, &out->full_window, &in->current_window );
    out->current_window = inner;

    if( box2i_is_empty( &inner ) )
        return;

    int width = inner.max.x - inner.min.x + 1;

    for( int y = inner.min.y; y <= inner.max.y; y++ ) {
        rgba_f32 *row_out = video_get_pixel_f32( out, inner.min.x, y );
        rgba_f32 *row_in = video_get_pixel_f32( in, inner.min.x, y );

        memcpy( row_out, row_in, sizeof(rgba_f32) * width );

        if( alpha != 1.0f ) {
            for( int x = 0; x < width; x++ )
                row_out[x].a *= alpha;
        }
    }
}

EXPORT void
video_mix_cross_f32( rgba_frame_f32 *out, rgba_frame_f32 *a, rgba_frame_f32 *b, float mix_b ) {
    box2i *awin = &a->current_window, *bwin = &b->current_window;

    mix_b = clampf(mix_b, 0.0f, 1.0f);
    const float mix_a = (1.0f - mix_b);

    if( box2i_is_empty( awin ) ) {
        video_copy_frame_alpha_f32( out, b, mix_b );
        return;
    }
    else if( box2i_is_empty( bwin ) ) {
        video_copy_frame_alpha_f32( out, a, mix_a );
        return;
    }

    box2i outer, inner;

    box2i_union( &outer, awin, bwin );
    box2i_intersect( &outer, &outer, &out->full_window );

    box2i_intersect( &inner, awin, bwin );
    box2i_intersect( &inner, &inner, &out->full_window );

    bool empty_inner_x = inner.min.x > inner.max.x,
        empty_inner_y = inner.min.y > inner.max.y;
    box2i_normalize( &inner );

    rgba_frame_f32 *top = (awin->min.y < bwin->min.y) ? a : b,
        *bottom = (awin->max.y > bwin->max.y) ? a : b,
        *left = (awin->min.x < bwin->min.y) ? a : b,
        *right = (awin->max.x > bwin->max.x) ? a : b;

    const rgba_f32 zero = { 0.0, 0.0, 0.0, 0.0 };

    // Top: one frame or the other is up here
    for( int y = outer.min.y; y < inner.min.y; y++ ) {
        rgba_f32 *row_out = video_get_pixel_f32( out, 0, y );
        rgba_f32 *row_top = video_get_pixel_f32( top, 0, y );
        const float mix = (top == a) ? mix_a : mix_b;

        for( int x = outer.min.x; x < top->current_window.min.x; x++ )
            row_out[x] = zero;

        for( int x = top->current_window.min.x; x <= top->current_window.max.x; x++ ) {
            row_out[x] = row_top[x];
            row_out[x].a *= mix;
        }

        for( int x = top->current_window.max.x + 1; x <= outer.max.x; x++ )
            row_out[x] = zero;
    }

    // Middle
    if( empty_inner_y ) {
        // Neither frame appears here
        for( int y = inner.min.y; y <= inner.max.y; y++ ) {
            rgba_f32 *row_out = video_get_pixel_f32( out, 0, y );

            for( int x = inner.min.x; x <= inner.max.x; x++ )
                row_out[x] = zero;
        }
    }
    else {
        // Both frames appear and might (or might not!) intersect
        const float mix_left = (left == a) ? mix_a : mix_b,
            mix_right = (right == a) ? mix_a : mix_b;

        for( int y = inner.min.y; y <= inner.max.y; y++ ) {
            rgba_f32 *row_out = video_get_pixel_f32( out, 0, y );
            rgba_f32 *row_a = video_get_pixel_f32( a, 0, y );
            rgba_f32 *row_b = video_get_pixel_f32( b, 0, y );
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
        rgba_f32 *row_out = video_get_pixel_f32( out, 0, y );
        rgba_f32 *row_bottom = video_get_pixel_f32( bottom, 0, y );
        const float mix = (bottom == a) ? mix_a : mix_b;

        for( int x = outer.min.x; x < bottom->current_window.min.x; x++ )
            row_out[x] = zero;

        for( int x = bottom->current_window.min.x; x <= bottom->current_window.max.x; x++ ) {
            row_out[x] = row_bottom[x];
            row_out[x].a *= mix;
        }

        for( int x = bottom->current_window.max.x + 1; x <= outer.max.x; x++ )
            row_out[x] = zero;
    }

    out->current_window = outer;
}

EXPORT void
video_mix_over_f32( rgba_frame_f32 *out, rgba_frame_f32 *b, float mix_b ) {
    box2i *outwin = &out->current_window, *bwin = &b->current_window;

    mix_b = clampf(mix_b, 0.0f, 1.0f);

    if( box2i_is_empty( outwin ) ) {
        video_copy_frame_alpha_f32( out, b, mix_b );
        return;
    }
    else if( box2i_is_empty( bwin ) || mix_b == 0.0f ) {
        return;
    }

    box2i outer, inner;

    box2i_union( &outer, outwin, bwin );
    box2i_intersect( &outer, &outer, &out->full_window );

    box2i_intersect( &inner, outwin, bwin );
    box2i_intersect( &inner, &inner, &out->full_window );

    bool empty_inner_x = inner.min.x > inner.max.x,
        empty_inner_y = inner.min.y > inner.max.y;
    box2i_normalize( &inner );

    rgba_frame_f32 *top = (outwin->min.y < bwin->min.y) ? out : b,
        *bottom = (outwin->max.y > bwin->max.y) ? out : b,
        *left = (outwin->min.x < bwin->min.y) ? out : b,
        *right = (outwin->max.x > bwin->max.x) ? out : b;

    const rgba_f32 zero = { 0.0, 0.0, 0.0, 0.0 };

    // Top: one frame or the other is up here
    for( int y = outer.min.y; y < inner.min.y; y++ ) {
        rgba_f32 *row_out = video_get_pixel_f32( out, 0, y );
        rgba_f32 *row_top = video_get_pixel_f32( top, 0, y );
        const float mix = (top == out) ? 1.0f : mix_b;

        for( int x = outer.min.x; x < top->current_window.min.x; x++ )
            row_out[x] = zero;

        if( top != out ) {
            for( int x = top->current_window.min.x; x <= top->current_window.max.x; x++ ) {
                row_out[x] = row_top[x];
                row_out[x].a *= mix;
            }
        }

        for( int x = top->current_window.max.x + 1; x <= outer.max.x; x++ )
            row_out[x] = zero;
    }

    // Middle
    if( empty_inner_y ) {
        // Neither frame appears here
        for( int y = inner.min.y; y <= inner.max.y; y++ ) {
            rgba_f32 *row_out = video_get_pixel_f32( out, 0, y );

            for( int x = inner.min.x; x <= inner.max.x; x++ )
                row_out[x] = zero;
        }
    }
    else {
        // Both frames appear and might (or might not!) intersect
        const float mix_left = (left == out) ? 1.0f : mix_b,
            mix_right = (right == out) ? 1.0f : mix_b;

        for( int y = inner.min.y; y <= inner.max.y; y++ ) {
            rgba_f32 *row_out = video_get_pixel_f32( out, 0, y );
            rgba_f32 *row_b = video_get_pixel_f32( b, 0, y );
            rgba_f32 *row_left = (left == out) ? row_out : row_b,
                *row_right = (right == out) ? row_out : row_b;

            if( out != left ) {
                for( int x = outer.min.x; x < inner.min.x; x++ ) {
                    row_out[x] = row_left[x];
                    row_out[x].a *= mix_left;
                }
            }

            if( empty_inner_x ) {
                for( int x = inner.min.x; x <= inner.max.x; x++ )
                    row_out[x] = zero;
            }
            else {
                for( int x = inner.min.x; x <= inner.max.x; x++ ) {
                    float alpha_b = row_b[x].a * mix_b;
                    float alpha_a = row_out[x].a * (1.0f - row_b[x].a * mix_b);

                    row_out[x].a = alpha_a + alpha_b;

                    if( row_out[x].a != 0.0 ) {
                        row_out[x].r = (row_out[x].r * alpha_a + row_b[x].r * alpha_b) / row_out[x].a;
                        row_out[x].g = (row_out[x].g * alpha_a + row_b[x].g * alpha_b) / row_out[x].a;
                        row_out[x].b = (row_out[x].b * alpha_a + row_b[x].b * alpha_b) / row_out[x].a;
                    }
                    else {
                        row_out[x] = zero;
                    }
                }
            }

            if( right != out ) {
                for( int x = inner.max.x + 1; x <= outer.max.x; x++ ) {
                    row_out[x] = row_right[x];
                    row_out[x].a *= mix_right;
                }
            }
        }
    }

    // Bottom: one frame or the other is down here
    for( int y = inner.max.y + 1; y <= outer.max.y; y++ ) {
        rgba_f32 *row_out = video_get_pixel_f32( out, 0, y );
        rgba_f32 *row_bottom = video_get_pixel_f32( bottom, 0, y );
        const float mix = (bottom == out) ? 1.0f : mix_b;

        for( int x = outer.min.x; x < bottom->current_window.min.x; x++ )
            row_out[x] = zero;

        if( bottom != out ) {
            for( int x = bottom->current_window.min.x; x <= bottom->current_window.max.x; x++ ) {
                row_out[x] = row_bottom[x];
                row_out[x].a *= mix;
            }
        }

        for( int x = bottom->current_window.max.x + 1; x <= outer.max.x; x++ )
            row_out[x] = zero;
    }

    out->current_window = outer;
}

// This crossfade is based on the associative alpha blending formula from:
//    http://en.wikipedia.org/w/index.php?title=Alpha_compositing&oldid=337850364

static const char *crossfade_shader_text =
"#version 110\n"
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect input_texture[" G_STRINGIFY(VIDEO_MAX_FILTER_INPUTS) "];\n"
"varying vec2 tex_coord[" G_STRINGIFY(VIDEO_MAX_FILTER_INPUTS) "];\n"
"uniform float mix_b;"
""
"void main() {"
"    vec4 color_a = texture2DRect(input_texture[0], tex_coord[0]);"
"    vec4 color_b = texture2DRect(input_texture[1], tex_coord[1]);"
""
"    float alpha_a = color_a.a * (1.0 - mix_b);"
"    float alpha_b = color_b.a * mix_b;"
""
"    gl_FragColor.a = alpha_a + alpha_b;"
""
"    if( gl_FragColor.a != 0.0 )"
"        gl_FragColor.rgb = (color_a.rgb * alpha_a + color_b.rgb * alpha_b) / gl_FragColor.a;"
"    else"
"        gl_FragColor.rgb = vec3(0.0, 0.0, 0.0);"
"}";

typedef struct {
    video_filter_program *program;
    GLint mix_b_uniform;
} gl_shader_state;

static void destroy_shader( gl_shader_state *shader ) {
    // We assume that we're in the right GL context
    video_delete_filter_program( shader->program );
    g_free( shader );
}

EXPORT void
video_mix_cross_gl( rgba_frame_gl *out, rgba_frame_gl *a, rgba_frame_gl *b, float mix_b ) {
    GQuark shader_quark = g_quark_from_static_string( "cprocess::video_mix::crossfade_shader" );
    g_debug( "In video_mix_cross_gl, mix_b=%f", mix_b );

    // Gather the mix factor
    mix_b = clampf(mix_b, 0.0f, 1.0f);

    void *context = getCurrentGLContext();
    gl_shader_state *shader = (gl_shader_state *) g_dataset_id_get_data( context, shader_quark );

    if( !shader ) {
        // Time to create the program for this context
        shader = g_new0( gl_shader_state, 1 );

        shader->program = video_create_filter_program( crossfade_shader_text, "Video mix crossfade shader" );
        shader->mix_b_uniform = glGetUniformLocation( shader->program->program, "mix_b" );

        g_dataset_id_set_data_full( context, shader_quark, shader, (GDestroyNotify) destroy_shader );
    }

    glUseProgram( shader->program->program );
    glUniform1f( shader->mix_b_uniform, mix_b );

    video_render_gl_frame_filter2( shader->program, out, a, b );
}

EXPORT void
video_mix_cross_gl_pull( rgba_frame_gl *out, video_source *a, int frame_a, video_source *b, int frame_b, float mix_b ) {
    // Gather the mix factor
    g_debug( "In video_mix_cross_gl_pull, mix_b=%f", mix_b );
    mix_b = clampf(mix_b, 0.0f, 1.0f);

    if( mix_b == 1.0f ) {
        // We only need frame B
        video_get_frame_gl( b, frame_b, out );
        return;
    }
    else if( mix_b == 0.0f ) {
        video_get_frame_gl( a, frame_a, out );
        return;
    }

    rgba_frame_gl fa = *out, fb = *out;

    video_get_frame_gl( a, frame_a, &fa );
    video_get_frame_gl( b, frame_b, &fb );

    video_mix_cross_gl( out, &fa, &fb, mix_b );

    glDeleteTextures( 1, &fa.texture );
    glDeleteTextures( 1, &fb.texture );
}

static const char *over_shader_text =
"#version 110\n"
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect input_texture[" G_STRINGIFY(VIDEO_MAX_FILTER_INPUTS) "];\n"
"varying vec2 tex_coord[" G_STRINGIFY(VIDEO_MAX_FILTER_INPUTS) "];\n"
"uniform float mix_b;"
""
"void main() {"
"    vec4 color_a = texture2DRect(input_texture[0], tex_coord[0]);"
"    vec4 color_b = texture2DRect(input_texture[1], tex_coord[1]);"
""
"    float alpha_a = color_a.a * (1.0f - color_b.a * mix_b);"
"    float alpha_b = color_b.a * mix_b;"
""
"    gl_FragColor.a = alpha_a + alpha_b;"
""
"    if( gl_FragColor.a != 0.0 )"
"        gl_FragColor.rgb = (color_a.rgb * alpha_a + color_b.rgb * alpha_b) / gl_FragColor.a;"
"    else"
"        gl_FragColor.rgb = vec3(0.0, 0.0, 0.0);"
"}";

EXPORT void
video_mix_over_gl( rgba_frame_gl *out, rgba_frame_gl *a, rgba_frame_gl *b, float mix_b ) {
    // Really, this is almost exactly the same code as video_mix_cross_gl; the two
    // should probably be combined (difference is just the shader text)
    GQuark shader_quark = g_quark_from_static_string( "cprocess::video_mix::over_shader" );

    // Gather the mix factor
    mix_b = clampf(mix_b, 0.0f, 1.0f);

    void *context = getCurrentGLContext();
    gl_shader_state *shader = (gl_shader_state *) g_dataset_id_get_data( context, shader_quark );

    if( !shader ) {
        // Time to create the program for this context
        shader = g_new0( gl_shader_state, 1 );

        shader->program = video_create_filter_program( over_shader_text, "Video mix over shader" );
        shader->mix_b_uniform = glGetUniformLocation( shader->program->program, "mix_b" );

        g_dataset_id_set_data_full( context, shader_quark, shader, (GDestroyNotify) destroy_shader );
    }

    glUseProgram( shader->program->program );
    glUniform1f( shader->mix_b_uniform, mix_b );

    video_render_gl_frame_filter2( shader->program, out, a, b );
}

