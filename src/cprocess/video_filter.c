/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2010-2 Brian J. Crowell <brian@fluggo.com>

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
#define G_LOG_DOMAIN "fluggo.media.cprocess.video_filter"

typedef struct {
    GLuint shader, program;
} gl_shader_state;

static void destroy_shader( gl_shader_state *shader ) {
    // We assume that we're in the right GL context
    glDeleteProgram( shader->program );
    glDeleteShader( shader->shader );
    g_free( shader );
}

static const char *gain_offset_shader_text =
"#version 120\n"
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect input_texture[" G_STRINGIFY(VIDEO_MAX_FILTER_INPUTS) "];\n"
"varying vec2 tex_coord[" G_STRINGIFY(VIDEO_MAX_FILTER_INPUTS) "];\n"
"uniform float gain, offset;"
""
"void main() {"
"    vec4 color = texture2DRect( input_texture[0], tex_coord[0] );"
""
"    gl_FragColor = color * vec4(gain, gain, gain, 1.0)"
"        + vec4(offset, offset, offset, 0.0);"
"}";

typedef struct {
    video_filter_program *program;
    GLuint gain, offset;
} gl_gain_offset_shader_state;

EXPORT void
video_filter_gain_offset_gl( rgba_frame_gl *out, rgba_frame_gl *input, float gain, float offset ) {
    GQuark shader_quark = g_quark_from_static_string( "cprocess::video_filter::gain_offset_shader" );

    void *context = getCurrentGLContext();
    gl_gain_offset_shader_state *shader = (gl_gain_offset_shader_state *) g_dataset_id_get_data( context, shader_quark );

    if( !shader ) {
        // Time to create the program for this context
        shader = g_new0( gl_gain_offset_shader_state, 1 );

        shader->program = video_create_filter_program( gain_offset_shader_text,
            "Fluggo Gain/Offset shader" );

        shader->gain = glGetUniformLocation( shader->program->program, "gain" );
        shader->offset = glGetUniformLocation( shader->program->program, "offset" );

        g_dataset_id_set_data_full( context, shader_quark, shader, (GDestroyNotify) destroy_shader );
    }

    glUseProgram( shader->program->program );
    glUniform1f( shader->gain, gain );
    glUniform1f( shader->offset, offset );

    video_render_gl_frame_filter1( shader->program, out, input );
}

static const char *crop_shader_text =
"#version 110\n"
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect input;"
"uniform vec4 crop_rect;"
""
"void main() {"
"    vec4 color = texture2DRect( input, gl_FragCoord.st );"
""
"    if( gl_FragCoord.s < crop_rect.x || gl_FragCoord.s > crop_rect.y ||"
"        gl_FragCoord.t < crop_rect.z || gl_FragCoord.t > crop_rect.w )"
"        color = vec4(0.0);"
""
"    gl_FragColor = color;"
"}";

typedef struct {
    gl_shader_state state;
    int input, crop_rect;
} gl_crop_shader_state;

EXPORT void
video_filter_crop_gl( rgba_frame_gl *out, rgba_frame_gl *input, box2i *crop_rect ) {
    GQuark shader_quark = g_quark_from_static_string( "cprocess::video_filter::crop_shader" );

    void *context = getCurrentGLContext();
    gl_crop_shader_state *shader = (gl_crop_shader_state *) g_dataset_id_get_data( context, shader_quark );

    if( !shader ) {
        // Time to create the program for this context
        shader = g_new0( gl_crop_shader_state, 1 );

        gl_buildShader( crop_shader_text, &shader->state.shader, &shader->state.program );

        shader->input = glGetUniformLocation( shader->state.program, "input" );
        shader->crop_rect = glGetUniformLocation( shader->state.program, "crop_rect" );

        g_dataset_id_set_data_full( context, shader_quark, shader, (GDestroyNotify) destroy_shader );
    }

    glUseProgram( shader->state.program );
    glUniform1i( shader->input, 0 );

    float crop_rect_f[4] = {
        (float) crop_rect->min.x,
        (float) crop_rect->min.y,
        (float) crop_rect->max.x,
        (float) crop_rect->max.y,
    };

    glUniform4fv( shader->crop_rect, 1, crop_rect_f );

    // Now set up the texture to render to
    v2i frame_size;
    box2i_get_size( &out->full_window, &frame_size );
    out->current_window = input->current_window;

    out->texture = video_make_gl_texture( frame_size.x, frame_size.y, NULL );

    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, input->texture );
    glEnable( GL_TEXTURE_RECTANGLE_ARB );

    gl_renderToTexture( out );

    glDisable( GL_TEXTURE_RECTANGLE_ARB );
    glUseProgram( 0 );
}


