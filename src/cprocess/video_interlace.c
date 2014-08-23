/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2013 Brian J. Crowell <brian@fluggo.com>

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
#define G_LOG_DOMAIN "fluggo.media.cprocess.video_interlace"

static const char *bob_deinterlace_shader_text =
"#version 120\n"
"#extension GL_ARB_texture_rectangle : enable\n"
"varying vec2 tex_coord[" G_STRINGIFY(VIDEO_MAX_FILTER_INPUTS) "];\n"
"uniform sampler2DRect input_texture[" G_STRINGIFY(VIDEO_MAX_FILTER_INPUTS) "];\n"
"varying vec2 frame_coord;\n"
"uniform bool upper_field;\n"
"\n"
"float oneIfOdd(float x) {\n"
"   // Returns one for odd (or near-odd), zero for even (or near-even)\n"
"   return max(0.0, sign(fract(x * 0.5) - 0.45));\n"
"}\n"
"\n"
"void main() {\n"
"   float upper_y, lower_y;\n"
"\n"
"   if(upper_field) {\n"
"       upper_y = tex_coord[0].y - oneIfOdd(frame_coord.y + 0.1);\n"
"       lower_y = tex_coord[0].y + oneIfOdd(frame_coord.y + 0.1);\n"
"   }\n"
"   else {\n"
"       upper_y = tex_coord[0].y - (1.0 - oneIfOdd(frame_coord.y + 0.1));\n"
"       lower_y = tex_coord[0].y + (1.0 - oneIfOdd(frame_coord.y + 0.1));\n"
"   }\n"
"\n"
//"   gl_FragColor = vec4(oneIfOdd(frame_coord.y + 0.1), 0.0, 0.0, 1.0);\n"
//"   gl_FragColor = vec4(fract(frame_coord.y+0.1), 0.0, 0.0, 1.0);\n"
//"   gl_FragColor = vec4(0.0, frame_coord.y / 480.0, 0.0, 1.0);\n"
"   gl_FragColor = mix(\n"
"       texture2DRect(input_texture[0], vec2(tex_coord[0].x, upper_y)),\n"
"       texture2DRect(input_texture[0], vec2(tex_coord[0].x, lower_y)),\n"
"       0.5);\n"
"}\n";

typedef struct {
    video_filter_program *program;
    GLuint upper_field_uniform;
} gl_bob_deinterlace_shader_state;

static void destroy_shader( gl_bob_deinterlace_shader_state *shader ) {
    // We assume that we're in the right GL context
    video_delete_filter_program( shader->program );
    g_free( shader );
}

EXPORT void
video_deinterlace_bob_gl( rgba_frame_gl *out, rgba_frame_gl *in, bool upper_field ) {
    GQuark shader_quark = g_quark_from_static_string( "cprocess::video_interlace::bob_deinterlace_shader" );

    if( !in ) {
        box2i_set_empty( &out->current_window );
        return;
    }

    void *context = getCurrentGLContext();
    gl_bob_deinterlace_shader_state *shader = (gl_bob_deinterlace_shader_state *) g_dataset_id_get_data( context, shader_quark );

    if( !shader ) {
        // Time to create the program for this context
        shader = g_new0( gl_bob_deinterlace_shader_state, 1 );

        shader->program = video_create_filter_program( bob_deinterlace_shader_text,
            "Fluggo Bob Deinterlace shader" );
        shader->upper_field_uniform = glGetUniformLocation( shader->program->program, "upper_field" );

        g_dataset_id_set_data_full( context, shader_quark, shader, (GDestroyNotify) destroy_shader );
    }

    glUseProgram( shader->program->program );
    glUniform1i( shader->upper_field_uniform, upper_field ? 1 : 0 );

    video_render_gl_frame_filter1( shader->program, out, in );
}

