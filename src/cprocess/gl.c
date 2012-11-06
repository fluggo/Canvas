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
    Make a suitable GL texture for a video frame, and bind it to the current stage.

    The texture ID should already be allocated from glGenTextures. *data* is optional;
    if supplied, it should be a straight array of [width*height] colors.

    The key things here are: half-float, RGBA, texture rectangle, and
    clamp-to-transparent on the edges.
*/
EXPORT void
video_make_gl_texture( GLuint texture, int width, int height, rgba_f16 *data ) {
    const float black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, texture );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA_FLOAT16_ATI, width, height, 0,
        GL_RGBA, GL_HALF_FLOAT_ARB, data );
    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );
    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );
    glTexParameterfv( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_BORDER_COLOR, black );
}

EXPORT void
gl_printShaderErrors( GLhandleARB shader ) {
    int status;
    glGetObjectParameterivARB( shader, GL_OBJECT_COMPILE_STATUS_ARB, &status );

    if( !status ) {
        g_warning( "Error(s) compiling the shader:\n" );
        int infoLogLength;

        glGetObjectParameterivARB( shader, GL_OBJECT_INFO_LOG_LENGTH_ARB, &infoLogLength );

        char *infoLog = g_malloc0( infoLogLength + 1 );

        glGetInfoLogARB( shader, infoLogLength, &infoLogLength, infoLog );

        g_warning( "%s\n", infoLog );
        g_free( infoLog );
    }
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

EXPORT void video_get_frame_gl( video_source *source, int frameIndex, rgba_frame_gl *targetFrame ) {
    if( !source || !source->funcs ) {
        // Even empty video sources need to produce a texture
        v2i frameSize;
        box2i_get_size( &targetFrame->full_window, &frameSize );

        glGenTextures( 1, &targetFrame->texture );
        glActiveTexture( GL_TEXTURE0 );
        video_make_gl_texture( targetFrame->texture, frameSize.x, frameSize.y, NULL );

        // Clear the texture (is there a faster way to do this?)
        // BJC: If we kill current_window, as below, we can just upload a dumb 1x1 pixel texture
        GLuint fbo;
        glGenFramebuffersEXT( 1, &fbo );
        glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, fbo );
        glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_RECTANGLE_ARB, targetFrame->texture, 0 );

        glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
        glClear( GL_COLOR_BUFFER_BIT );

        glDeleteFramebuffersEXT( 1, &fbo );

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
    glGenTextures( 1, &targetFrame->texture );
    video_make_gl_texture( targetFrame->texture, frameSize.x, frameSize.y, frame.data );

    g_slice_free1( sizeof(rgba_f16) * frameSize.x * frameSize.y, frame.data );
}


