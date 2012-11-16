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

#include "framework.h"

#include <GL/glew.h>
#include <GL/gl.h>

#if defined(WINNT)
#include <windows.h>
#else
#include <GL/glx.h>
#endif

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "fluggo.media.cprocess.gl"

static gsize __glew_init = 0;

static void
gl_ensure_glew() {
    if( g_once_init_enter( &__glew_init ) ) {
        g_debug( "Initializing GLEW" );
        glewInit();

        g_once_init_leave( &__glew_init, 1 );
    }
}

#if !defined(WINNT)
static Display *__display = NULL;

typedef struct {
    GLXContext context;
    GLXPbuffer pbuffer;
} glx_context_holder;

EXPORT void *
gl_create_offscreen_context() {
    // Create a GL context suitable for rendering offscreen
    // TODO: Run glewInit() after this
    if( g_once_init_enter( &__display ) ) {
        g_debug( "Opening X display..." );
        Display *display = XOpenDisplay( NULL );

        if( !display )
            g_error( "Could not open X display." );

        g_once_init_leave( &__display, display );
    }

    int fb_attrs[] = {
        GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT, None };

    g_debug( "Choosing framebuffer config" );
    int config_count = 0;
    GLXFBConfig *configs = glXChooseFBConfig(
        __display, DefaultScreen( __display ), fb_attrs, &config_count );

    if( !config_count )
        g_error( "No frame buffer configurations available. Which is weird." );

    GLXContext new_context = glXCreateNewContext(
        __display, configs[0], GLX_RGBA_TYPE, NULL, True );

    if( !new_context )
        g_error( "Failed to create context." );

    int pbuf_attrs[] = { GLX_PBUFFER_WIDTH, 1, GLX_PBUFFER_HEIGHT, 1, GLX_PRESERVED_CONTENTS, False, None };

    GLXPbuffer pbuf = glXCreatePbuffer( __display, configs[0], pbuf_attrs );

    if( pbuf == None )
        g_error( "Failed to create XGL pixel buffer." );

    glx_context_holder *result = g_new( glx_context_holder, 1 );

    result->context = new_context;
    result->pbuffer = pbuf;

    return result;
}

EXPORT void
gl_destroy_offscreen_context( void *context ) {
    glx_context_holder *holder = (glx_context_holder *) context;

    // Clean up attached resources
    gl_set_current_context( context );
    g_dataset_destroy( holder->context );
    gl_set_current_context( NULL );

    glXDestroyPbuffer( __display, holder->pbuffer );
    glXDestroyContext( __display, holder->context );

    g_free( holder );
}

EXPORT void
gl_set_current_context( void *context ) {
    glx_context_holder *holder = (glx_context_holder *) context;

    if( !holder ) {
        if( !glXMakeContextCurrent( __display,
            None,
            None,
            NULL ) ) {
            g_error( "Failed to set null context." );
        }
    }

    if( !glXMakeContextCurrent( __display,
        holder->pbuffer,
        holder->pbuffer,
        holder->context ) ) {
        g_error( "Failed to set context current." );
    }
}
#endif

static GPrivate __thread_context = G_PRIVATE_INIT(gl_destroy_offscreen_context);

EXPORT void *
gl_create_thread_offscreen_context() {
    // Ensure that a GL offscreen context is associated with this thread.
    // This context will be used if no context is current at the time of a video
    // fetch call.
    void *context = g_private_get( &__thread_context );

    if( !context ) {
        context = gl_create_offscreen_context();
        g_private_replace( &__thread_context, context );
    }

    return context;
}

EXPORT void
gl_ensure_context() {
    // Ensures that *a* context-- our thread-local or someone else's-- is current.
    g_debug( "gl_ensure_context() called" );
    if( !getCurrentGLContext() ) {
        g_debug( "No current context, going to check for a thread context..." );
        void *context = gl_create_thread_offscreen_context();
        gl_set_current_context( context );
    }

    gl_ensure_glew();
}

EXPORT void
gl_destroy_thread_offscreen_context() {
    // The context should automatically get cleaned up when the thread finishes,
    // but in case someone wants to clean it up early:
    g_private_replace( &__thread_context, NULL );
}


EXPORT void
__gl_checkError(const char *file, const unsigned long line) {
    int error = glGetError();

    switch( error ) {
        case GL_NO_ERROR:
            return;

        case GL_INVALID_OPERATION:
            g_warning( "%s:%lu: Invalid operation", file, line );
            return;

        case GL_INVALID_VALUE:
            g_warning( "%s:%lu: Invalid value", file, line );
            return;

        case GL_INVALID_ENUM:
            g_warning( "%s:%lu: Invalid enum", file, line );
            return;

        default:
            g_warning( "%s:%lu: Other GL error", file, line );
            return;
    }
}

EXPORT void *getCurrentGLContext() {
#if defined(WINNT)
    return wglGetCurrentContext();
#else
    return glXGetCurrentContext();
#endif
}

/*
    Function: gl_renderToTexture
    Render a quad to the given GL frame.

    Parameters:

    frame - Frame to render to, containing a valid full_window.
            gl_renderToTexture won't alter the current_window.

    Remarks:
    This function creates a GL texture for the frame and renders a quad
    with the current context settings to it.
*/
EXPORT void
gl_renderToTexture( rgba_frame_gl *frame ) {
    v2i frameSize;
    box2i_get_size( &frame->full_window, &frameSize );

    g_assert( frameSize.x > 0 );
    g_assert( frameSize.y > 0 );

    // TODO: It's a rumor that creating and destroying framebuffers is bad for
    // performance; we do it here maybe a dozen times per frame
    GLuint fbo;
    glGenFramebuffersEXT( 1, &fbo );
    glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, fbo );
    glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB,
        frame->texture, 0 );

    glLoadIdentity();
    glOrtho( 0, frameSize.x, 0, frameSize.y, -1, 1 );
    glViewport( 0, 0, frameSize.x, frameSize.y );

    glBegin( GL_QUADS );
    glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
    glTexCoord2i( 0, 0 );
    glVertex2i( 0, 0 );
    glTexCoord2i( frameSize.x, 0 );
    glVertex2i( frameSize.x, 0 );
    glTexCoord2i( frameSize.x, frameSize.y );
    glVertex2i( frameSize.x, frameSize.y );
    glTexCoord2i( 0, frameSize.y );
    glVertex2i( 0, frameSize.y );
    glEnd();

    glDeleteFramebuffersEXT( 1, &fbo );
}

/*
    Make a suitable GL texture for a video frame.

    *data* is optional; if supplied, it should be a straight array of [width*height]
    colors. If not supplied, the texture will be undefined.

    The key things here are: half-float, RGBA, texture rectangle, and
    clamp-to-transparent on the edges.
*/
EXPORT GLuint
video_make_gl_texture( int width, int height, rgba_f16 *data ) {
    const float black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    GLuint texture;

    glGenTextures( 1, &texture );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, texture );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA_FLOAT16_ATI, width, height, 0,
        GL_RGBA, GL_HALF_FLOAT_ARB, data );
    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );
    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );
    glTexParameterfv( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_BORDER_COLOR, black );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );

    return texture;
}

EXPORT GLuint
gl_compile_shader( GLenum shader_type, const char *source, const char *name ) {
    GLuint shader = glCreateShader( shader_type );
    glShaderSource( shader, 1, &source, NULL );
    glCompileShader( shader );

    GLint status;
    glGetShaderiv( shader, GL_COMPILE_STATUS, &status );

    GLsizei length;
    GLint length_param;

    glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &length_param );
    length = length_param;

    GLchar *log = g_malloc0( length );

    glGetShaderInfoLog( shader, length, &length, log );

    if( !status ) {
        g_warning( "Error(s) compiling the shader \"%s\":\n%s\n", name, log );
    }
    else {
        g_message( "Info from compiling the shader \"%s\":\n%s\n", name, log );
    }

    g_free( log );
    return shader;
}

EXPORT GLuint
gl_link_program( const GLuint *shaders, int shader_count, const char *name ) {
    GLuint program = glCreateProgram();

    for( int i = 0; i < shader_count; i++ )
        glAttachShader( program, shaders[i] );

    glLinkProgram( program );

    GLint status;
    glGetProgramiv( program, GL_LINK_STATUS, &status );

    GLsizei length;
    GLint length_param;

    glGetProgramiv( program, GL_INFO_LOG_LENGTH, &length_param );
    length = length_param;

    GLchar *log = g_malloc0( length );

    glGetProgramInfoLog( program, length, &length, log );

    if( !status ) {
        g_warning( "Error(s) linking the program \"%s\":\n%s\n", name, log );
    }
    else {
        g_message( "Info from linking the program \"%s\":\n%s\n", name, log );
    }

    g_free( log );
    return program;
}

EXPORT void
gl_buildShader( const char *source, GLuint *outShader, GLuint *outProgram ) {
    GLuint shader = glCreateShader( GL_FRAGMENT_SHADER );
    glShaderSource( shader, 1, &source, NULL );
    glCompileShader( shader );

    //gl_printShaderErrors( shader );

    GLuint program = glCreateProgram();
    glAttachShader( program, shader );
    glLinkProgram( program );

    *outShader = shader;
    *outProgram = program;
}

static const char *vertex_shader_text =
"#version 120\n"
"#extension GL_ARB_texture_rectangle : require\n"
"uniform ivec2 tex_offset[" G_STRINGIFY(VIDEO_MAX_FILTER_INPUTS) "];\n"
"uniform sampler2DRect input_texture[" G_STRINGIFY(VIDEO_MAX_FILTER_INPUTS) "];\n"
"uniform int input_count;\n"
"uniform ivec2 frame_size;\n"
"uniform ivec2 frame_offset;\n"
"attribute vec2 position;\n"
"varying vec2 tex_coord[" G_STRINGIFY(VIDEO_MAX_FILTER_INPUTS) "];\n"
"\n"
"void main() {\n"
"    gl_Position = vec4(position * vec2(2.0, 2.0) + vec2(-1.0, -1.0), 0.0, 1.0);\n"
"\n"
"    for( int i = 0; i < input_count; i++ ) {\n"
"        tex_coord[i] = position * vec2(frame_size) + frame_offset + tex_offset[i];\n"
"    }\n"
"}\n";

typedef struct {
    // These first two are the same fields as video_filter_program
    GLuint program, fragment_shader;
    GLuint tex_offset_uniform, frame_size_uniform, frame_offset_uniform,
        input_count_uniform, input_texture_uniform;
    GLuint position_attribute;
} user_filter_program;

typedef struct {
    GLuint shader;
    GLuint vertex_buffer;
} vertex_shader_state;

static void
destroy_vertex_shader( vertex_shader_state *state ) {
    glDeleteShader( state->shader );
    glDeleteBuffers( 1, &state->vertex_buffer );
    g_free( state );
}

static vertex_shader_state*
gl_get_vertex_shader() {
    GQuark shader_quark = g_quark_from_static_string( "cprocess::gl::vertex_shader" );

    void *context = getCurrentGLContext();
    vertex_shader_state *state = (vertex_shader_state *) g_dataset_id_get_data( context, shader_quark );

    if( !state ) {
        // Time to create the shader for this context
        state = g_new0( vertex_shader_state, 1 );

        state->shader = gl_compile_shader( GL_VERTEX_SHADER, vertex_shader_text,
            "Compositor vertex shader" );

        // Set up a static buffer with four corners for us to work with
        glGenBuffers( 1, &state->vertex_buffer );

        v2f positions[4] = {
            { 0.0f, 0.0f },
            { 1.0f, 0.0f },
            { 1.0f, 1.0f },
            { 0.0f, 1.0f }
        };

        glBindBuffer( GL_ARRAY_BUFFER, state->vertex_buffer );
        glBufferData( GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );

        g_dataset_id_set_data_full( context, shader_quark, state, (GDestroyNotify) destroy_vertex_shader );
    }

    return state;
}

/*
    Creates a filter program from the given fragment shader and a standard
    vertex shader that computes the correct texels for the given frame.
*/
EXPORT video_filter_program *
video_create_filter_program( const char *fragment_shader, const char *name ) {
    user_filter_program *program = g_new0( user_filter_program, 1 );

    vertex_shader_state *vshad = gl_get_vertex_shader();
    program->fragment_shader = gl_compile_shader( GL_FRAGMENT_SHADER, fragment_shader, name );

    GLuint shaders[2] = { vshad->shader, program->fragment_shader };
    program->program = gl_link_program( shaders, 2, name );

    program->tex_offset_uniform = glGetUniformLocation( program->program, "tex_offset" );
    program->frame_size_uniform = glGetUniformLocation( program->program, "frame_size" );
    program->frame_offset_uniform = glGetUniformLocation( program->program, "frame_offset" );
    program->input_count_uniform = glGetUniformLocation( program->program, "input_count" );
    program->input_texture_uniform = glGetUniformLocation( program->program, "input_texture" );
    program->position_attribute = glGetAttribLocation( program->program, "position" );

    return (video_filter_program *) program;
}

EXPORT void
video_delete_filter_program( video_filter_program *program ) {
    user_filter_program *up = (user_filter_program*) program;
    glDeleteShader( up->fragment_shader );
    glDeleteProgram( up->program );
    g_free( up );
}

EXPORT void
video_set_filter_uniforms( video_filter_program *program, box2i *out_full_window, box2i *in_full_windows[], int input_count ) {
    user_filter_program *up = (user_filter_program*) program;
    g_assert( input_count >= 0 );
    g_assert( input_count <= VIDEO_MAX_FILTER_INPUTS );

    int textures[VIDEO_MAX_FILTER_INPUTS];

    for( int i = 0; i < VIDEO_MAX_FILTER_INPUTS; i++ )
        textures[i] = i;

    glUniform1i( up->input_count_uniform, input_count );
    glUniform1iv( up->input_texture_uniform, VIDEO_MAX_FILTER_INPUTS, textures );

    v2i frame_size;
    box2i_get_size( out_full_window, &frame_size );
    glUniform2iv( up->frame_size_uniform, 1, &frame_size.x );
    glUniform2iv( up->frame_offset_uniform, 1, &out_full_window->min.x );

    // Calculate texture offsets
    v2i tex_offsets[VIDEO_MAX_FILTER_INPUTS];

    for( int i = 0; i < input_count && in_full_windows; i++ ) {
        v2i_subtract( &tex_offsets[i], &out_full_window->min,
            &in_full_windows[i]->min );
    }

    glUniform2iv( up->tex_offset_uniform, input_count, &tex_offsets[0].x );
}

typedef struct {
    GLuint framebuffer;
} framebuffer_state;

static void
destroy_framebuffer_state( framebuffer_state *state ) {
    glDeleteFramebuffersEXT( 1, &state->framebuffer );
    g_free( state );
}

static GLuint
gl_get_composite_framebuffer() {
    GQuark fb_quark = g_quark_from_static_string( "cprocess::gl::composite_framebuffer" );

    void *context = getCurrentGLContext();
    framebuffer_state *state = (framebuffer_state *) g_dataset_id_get_data( context, fb_quark );

    if( !state ) {
        // Create the compositing framebuffer for this context
        state = g_new0( framebuffer_state, 1 );

        glGenFramebuffersEXT( 1, &state->framebuffer );

        g_dataset_id_set_data_full( context, fb_quark, state, (GDestroyNotify) destroy_framebuffer_state );
    }

    return state->framebuffer;
}

/*
    Renders a GL frame with the current settings and the given program.

    This function assumes that all needed textures and uniforms are set before
    being called, and requires that the output frame's full_window is set.
    It does not allocate the output texture, and does not set current_window; you
    must do that yourself.

    program - Filter program to use. If you need to set custom uniforms, load the
        program and set the uniforms before calling this function. This function
        will overwrite the uniforms for the built-in compositor vertex shader.
    out - Frame to render into. Must have its full_window set. The current_window
        will be set to the current window of the input frame. A new texture will
        be generated for the result.
    in_full_windows - An array of pointers to the full_windows of the textures
        assigned to GL_TEXTURE0, GL_TEXTURE1, etc. These are used to generate
        texture coordinates in the tex_coord varying. May be NULL.
    input_count - The number of pointers in in_full_windows.
*/
EXPORT void
video_render_gl_frame( video_filter_program *program, rgba_frame_gl *out, box2i *in_full_windows[], int input_count ) {
    user_filter_program *up = (user_filter_program *) program;

    v2i frame_size;
    box2i_get_size( &out->full_window, &frame_size );

    // Set up the program
    glUseProgram( program->program );
    video_set_filter_uniforms( program, &out->full_window, in_full_windows, input_count );

    // Compose the texture
    vertex_shader_state *vshader = gl_get_vertex_shader();
    GLuint fbo = gl_get_composite_framebuffer();

    glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, fbo );
    glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
        GL_TEXTURE_RECTANGLE_ARB, out->texture, 0 );

    glBindBuffer( GL_ARRAY_BUFFER, vshader->vertex_buffer );
    glEnableVertexAttribArray( up->position_attribute );
    glVertexAttribPointer( up->position_attribute, 2, GL_FLOAT, GL_FALSE, 0, 0 );

    glViewport( 0, 0, frame_size.x, frame_size.y );

    glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );

    glDisableVertexAttribArray( up->position_attribute );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, 0 );
}

/*
    Renders a GL frame with the current settings and a filter with one input.

    This is the most convenient means of running a GL filter. It allocates the
    output frame and computes the current_window for you (as the intersection of
    the output frame's full_window and the input frame's current_window).

    program - Filter program to use. If you need to set custom uniforms, load the
        program and set the uniforms before calling this function. This function
        will overwrite the uniforms for the built-in compositor vertex shader.
    out - Frame to render into. Must have its full_window set. The current_window
        will be set to the current window of the input frame. A new texture will
        be generated for the result.
    in - Input frame.
*/
EXPORT void
video_render_gl_frame_filter1( video_filter_program *program, rgba_frame_gl *out, rgba_frame_gl *in ) {
    v2i frame_size;
    box2i_get_size( &out->full_window, &frame_size );
    box2i* in_windows[1] = { &in->full_window };

    out->texture = video_make_gl_texture( frame_size.x, frame_size.y, NULL );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, in->texture );

    video_render_gl_frame( program, out, in_windows, 1 );

    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );
    box2i_intersect( &out->current_window, &out->full_window, &in->current_window );
}

EXPORT void
video_get_frame_gl( video_source *source, int frameIndex, rgba_frame_gl *targetFrame ) {
    if( !source || !source->funcs ) {
        // Even empty video sources need to produce a texture
        GLuint fbo = gl_get_composite_framebuffer();

        v2i frameSize;
        box2i_get_size( &targetFrame->full_window, &frameSize );

        targetFrame->texture = video_make_gl_texture( frameSize.x, frameSize.y, NULL );

        // Clear the texture (is there a faster way to do this?)
        glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, fbo );
        glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_RECTANGLE_ARB, targetFrame->texture, 0 );

        glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
        glClear( GL_COLOR_BUFFER_BIT );

        glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, 0 );

        box2i_set_empty( &targetFrame->current_window );
        return;
    }

    if( source->funcs->get_frame_gl ) {
        source->funcs->get_frame_gl( source->obj, frameIndex, targetFrame );
        return;
    }

    // Pull 16-bit frame data from the software chain and load it
    v2i frameSize;
    box2i_get_size( &targetFrame->full_window, &frameSize );

    rgba_frame_f16 frame = { NULL };
    frame.data = g_slice_alloc0( sizeof(rgba_f16) * frameSize.x * frameSize.y );
    frame.full_window = targetFrame->full_window;
    frame.current_window = targetFrame->full_window;

    video_get_frame_f16( source, frameIndex, &frame );

    // TODO: Only fill in the area specified by current_window
    // BJC: A different solution would remove the distinction between full_window
    // and current_window for GL textures, which would be easy to do since the
    // upstream allocates the texture anyhow
    targetFrame->texture = video_make_gl_texture( frameSize.x, frameSize.y, frame.data );

    g_slice_free1( sizeof(rgba_f16) * frameSize.x * frameSize.y, frame.data );
}


