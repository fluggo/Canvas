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
#include <GL/glx.h>

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
    return glXGetCurrentContext();
}

/*
    Function: gl_renderToTexture
    Render a quad to the given GL frame.

    Parameters:

    frame - Frame to render to, containing a valid fullDataWindow.
            gl_renderToTexture won't alter the currentDataWindow.

    Remarks:
    This function creates a GL texture for the frame and renders a quad
    with the current context settings to it.
*/
EXPORT void
gl_renderToTexture( rgba_frame_gl *frame ) {
    v2i frameSize;
    box2i_getSize( &frame->full_window, &frameSize );

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
gl_buildShader( const char *source, GLhandleARB *outShader, GLhandleARB *outProgram ) {
    GLhandleARB shader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
    glShaderSourceARB( shader, 1, &source, NULL );
    glCompileShaderARB( shader );

    gl_printShaderErrors( shader );

    GLhandleARB program = glCreateProgramObjectARB();
    glAttachObjectARB( program, shader );
    glLinkProgramARB( program );

    *outShader = shader;
    *outProgram = program;
}

EXPORT void video_getFrame_gl( video_source *source, int frameIndex, rgba_frame_gl *targetFrame ) {
    if( !source || !source->funcs ) {
        box2i_setEmpty( &targetFrame->currentDataWindow );
        return;
    }

    if( source->funcs->getFrameGL ) {
        source->funcs->getFrameGL( source->obj, frameIndex, targetFrame );
        return;
    }

    // Pull 16-bit frame data from the software chain and load it
    v2i frameSize;
    box2i_getSize( &targetFrame->full_window, &frameSize );

    rgba_frame_f16 frame = { NULL };
    frame.data = g_slice_alloc0( sizeof(rgba_f16) * frameSize.x * frameSize.y );
    frame.full_window = targetFrame->full_window;
    frame.currentDataWindow = targetFrame->full_window;

    video_getFrame_f16( source, frameIndex, &frame );

    // TODO: Only fill in the area specified by currentDataWindow
    glGenTextures( 1, &targetFrame->texture );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, targetFrame->texture );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA_FLOAT16_ATI, frameSize.x, frameSize.y, 0,
        GL_RGBA, GL_HALF_FLOAT_ARB, frame.data );

    g_slice_free1( sizeof(rgba_f16) * frameSize.x * frameSize.y, frame.data );
}


