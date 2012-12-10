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

#include <stdlib.h>
#include <math.h>
#include "framework.h"

#define SOFT_MODE_BUFFERS    4
#define HARD_MODE_BUFFERS    2

typedef struct {
    uint8_t r, g, b;
} rgb8;

typedef struct {
    int64_t time, nextTime;
    rgba_u8 *frameData;
    int stride;
    box2i fullDataWindow, currentDataWindow;
} SoftFrameTarget;

struct __tag_widget_gl_context {
    invalidate_func invalidate_func;
    void *invalidate_closure;

    video_source *frameSource;
    presentation_clock clock;
    GStaticRWLock frame_read_rwlock;
    GMutex *frameReadMutex;
    GCond *frameReadCond;
    int nextToRenderFrame;
    int readBuffer, writeBuffer, filled;
    rational frameRate;
    guint timeoutSourceID;
    box2i displayWindow, currentDataWindow;
    int firstFrame, lastFrame;
    float pixelAspectRatio;
    bool renderOneFrame;
    int drawOneFrame;
    int lastHardFrame;

    rgb8 checkerColors[2];

    // True: render in software, false, render in hardware
    bool softMode;
    bool hardModeDisable, hardModeSupported;

    SoftFrameTarget softTargets[SOFT_MODE_BUFFERS];
    GLuint softTextureId, hardTextureId, checkerTextureId;

    // Number of buffers available
    int bufferCount;

    float rate;
    bool quit;
    GThread *renderThread;
    void *clock_callback_handle;

    uint8_t *gamma_ramp;
    float rendering_intent;
};

static gboolean
playSingleFrame( widget_gl_context *self ) {
    if( self->quit )
        return FALSE;

    if( self->softMode ) {
        if( self->filled > 0 || self->drawOneFrame ) {
            g_mutex_lock( self->frameReadMutex );
            // BJC: There's probably a better way of determining this, such as
            // whether or not we're playing
            bool was_draw_one_frame = self->drawOneFrame ? true : false;
            int draw_one_frame = self->drawOneFrame;
            int filled = self->filled;

            if( !draw_one_frame )
                self->readBuffer = (self->readBuffer + 1) % self->bufferCount;
            else
                self->drawOneFrame--;

            int64_t nextPresentationTime = self->softTargets[self->readBuffer].nextTime;
            g_mutex_unlock( self->frameReadMutex );

            if( filled != 0 || draw_one_frame ) {
                if( self->invalidate_func )
                    self->invalidate_func( self->invalidate_closure );

                //g_print( "Painted %d from %d...\n", get_time_frame( &self->frameRate, self->softTargets[self->readBuffer].time ), self->readBuffer );

                if( was_draw_one_frame ) {
                    // We're done here, go back to sleep
                    //g_print( "Drew one frame\n" );
                }
                else {
                    //g_print( "Preparing next frame\n" );
                    g_mutex_lock( self->frameReadMutex );

                    self->filled--;

                    g_cond_signal( self->frameReadCond );
                    g_mutex_unlock( self->frameReadMutex );

                    rational speed;
                    self->clock.funcs->getSpeed( self->clock.obj, &speed );

                    if( speed.n != 0 ) {
                        //printf( "nextPresent: %ld, current: %ld, baseTime: %ld, seekTime: %ld\n", nextPresentationTime, self->clock->getPresentationTime(), ((SystemPresentationClock*)self->clock)->_baseTime, ((SystemPresentationClock*) self->clock)->_seekTime );

                        int timeout = ((nextPresentationTime - self->clock.funcs->getPresentationTime( self->clock.obj )) * speed.d) / (speed.n * 1000000);

                        if( timeout < 0 )
                            timeout = 0;

                        self->timeoutSourceID = g_timeout_add_full(
                            G_PRIORITY_DEFAULT, timeout, (GSourceFunc) playSingleFrame, self, NULL );
                    }
                }

                return FALSE;
            }
        }
    }
    else {
        // Naive
        //printf( "Exposing...\n" );
        if( self->invalidate_func )
            self->invalidate_func( self->invalidate_closure );

        for( ;; ) {
            rational speed;
            self->clock.funcs->getSpeed( self->clock.obj, &speed );

            if( speed.n > 0 )
                self->nextToRenderFrame++;
            else if( speed.n < 0 )
                self->nextToRenderFrame--;
            else if( speed.n == 0 )
                return FALSE;

            int64_t nextPresentationTime = get_frame_time( &self->frameRate, self->nextToRenderFrame );

            //printf( "nextPresent: %ld, current: %ld, baseTime: %ld, seekTime: %ld\n", nextPresentationTime, self->clock->getPresentationTime(), ((SystemPresentationClock*)self->clock)->_baseTime, ((SystemPresentationClock*) self->clock)->_seekTime );

            int64_t timeout = ((nextPresentationTime - self->clock.funcs->getPresentationTime( self->clock.obj )) * speed.d) / (speed.n * INT64_C(1000000));

            if( timeout < 0 )
                continue;

            //printf( "Next frame at %d, timeout %d ms\n", self->nextToRenderFrame, (int) timeout );

            self->timeoutSourceID = g_timeout_add_full(
                G_PRIORITY_DEFAULT, (int) timeout, (GSourceFunc) playSingleFrame, self, NULL );

            return FALSE;
        }
    }

    rational speed;
    self->clock.funcs->getSpeed( self->clock.obj, &speed );

    if( speed.n != 0 ) {
        self->timeoutSourceID = g_timeout_add_full( G_PRIORITY_DEFAULT,
            (1000 * self->frameRate.d * speed.d) / (self->frameRate.n * abs(speed.n)),
            (GSourceFunc) playSingleFrame, self, NULL );
    }

    return FALSE;
}

EXPORT void
widget_gl_display_frame( widget_gl_context *self ) {
    playSingleFrame( self );
}

static bool box2i_equalSize( box2i *box1, box2i *box2 ) {
    v2i size1, size2;

    box2i_get_size( box1, &size1 );
    box2i_get_size( box2, &size2 );

    return size1.x == size2.x && size1.y == size2.y;
}

static gpointer
playbackThread( widget_gl_context *self ) {
    rgba_frame_f16 frame = { NULL };
    box2i_set_empty( &frame.full_window );

    for( ;; ) {
        int64_t startTime = INT64_C(0);

        if( self->clock.funcs )
            startTime = self->clock.funcs->getPresentationTime( self->clock.obj );

        g_mutex_lock( self->frameReadMutex );
        while( !self->clock.funcs || (!self->quit && ((!self->renderOneFrame && self->filled > 2) || !self->softMode)) )
            g_cond_wait( self->frameReadCond, self->frameReadMutex );

        if( self->quit ) {
            g_mutex_unlock( self->frameReadMutex );
            return NULL;
        }

        // If restarting, reset the clock; who knows how long we've been waiting?
        if( self->filled < 0 )
            startTime = self->clock.funcs->getPresentationTime( self->clock.obj );

        v2i frameSize;
        rational speed;

        box2i_get_size( &self->displayWindow, &frameSize );
        self->clock.funcs->getSpeed( self->clock.obj, &speed );

        // Pull out the next frame
        int nextFrame = self->nextToRenderFrame;

        // Clamp to the set first/last frames
        if( nextFrame > self->lastFrame )
            nextFrame = self->lastFrame;
        else if( nextFrame < self->firstFrame )
            nextFrame = self->firstFrame;

        // Set up the proper write buffer
        if( !self->renderOneFrame )
            self->writeBuffer = (self->writeBuffer + 1) % self->bufferCount;

        int writeBuffer = self->writeBuffer;

        SoftFrameTarget *target = &self->softTargets[writeBuffer];

        // If the frame is the wrong size, reallocate it now
        if( box2i_is_empty( &target->fullDataWindow ) ||
            !box2i_equalSize( &self->displayWindow, &target->fullDataWindow ) ) {

            g_free( target->frameData );
            target->frameData = g_malloc( frameSize.y * frameSize.x * sizeof(rgba_u8) );
            target->stride = frameSize.x;
        }

        // If our target array is the wrong size, reallocate it now
        if( box2i_is_empty( &frame.full_window ) ||
            !box2i_equalSize( &self->displayWindow, &frame.full_window ) ) {

            g_free( frame.data );
            frame.data = g_malloc( frameSize.y * frameSize.x * sizeof(rgba_f16) );
        }

        // The frame could shift without changing size, so we set this here just in case
        target->fullDataWindow = self->displayWindow;
        frame.full_window = self->displayWindow;

        bool wasRenderOneFrame = self->renderOneFrame;
        self->renderOneFrame = false;

        g_mutex_unlock( self->frameReadMutex );

//        printf( "Start rendering %d into %d...\n", nextFrame, writeBuffer );

        // Pull the frame data from the chain
        g_static_rw_lock_reader_lock( &self->frame_read_rwlock );
        if( self->frameSource != NULL ) {
            video_get_frame_f16( self->frameSource, nextFrame, &frame );
        }
        else {
            // No result
            box2i_set_empty( &frame.current_window );
        }
        g_static_rw_lock_reader_unlock( &self->frame_read_rwlock );

        target->currentDataWindow = frame.current_window;

        // Convert the results to 8-bit
        const uint8_t *gamma45 = self->gamma_ramp;

        for( int y = frame.current_window.min.y; y <= frame.current_window.max.y; y++ ) {
            rgba_u8 *targetData = &target->frameData[(y - target->fullDataWindow.min.y) * target->stride - frame.full_window.min.x];
            rgba_f16 *sourceData = video_get_pixel_f16( &frame, 0, y );

            video_transfer_linear_to_sRGB( &sourceData[frame.current_window.min.x].r,
                &sourceData[frame.current_window.min.x].r,
                (frame.current_window.max.x - frame.current_window.min.x + 1) * 4 );

            for( int x = frame.current_window.min.x; x <= frame.current_window.max.x; x++ ) {
                targetData[x].r = gamma45[sourceData[x].r];
                targetData[x].g = gamma45[sourceData[x].g];
                targetData[x].b = gamma45[sourceData[x].b];
                targetData[x].a = gamma45[sourceData[x].a];
            }
        }

        //usleep( 100000 );

        self->softTargets[writeBuffer].time = get_frame_time( &self->frameRate, nextFrame );
        int64_t endTime = self->clock.funcs->getPresentationTime( self->clock.obj );

        int64_t lastDuration = endTime - startTime;

        //printf( "Rendered frame %d into %d in %f presentation seconds (at %ld)...\n", self->nextToRenderFrame, writeBuffer,
        //    ((double) endTime - (double) startTime) / 1000000000.0, endTime );
        //printf( "Presentation time %ld\n", info->_presentationTime[writeBuffer] );

        g_mutex_lock( self->frameReadMutex );
        if( self->filled < 0 ) {
            rational newSpeed;
            self->clock.funcs->getSpeed( self->clock.obj, &newSpeed );

            if( speed.n * newSpeed.d != 0 )
                lastDuration = lastDuration * newSpeed.n * speed.d / (speed.n * newSpeed.d);
            else
                lastDuration = 0;

            speed = newSpeed;

            if( speed.n > 0 )
                self->nextToRenderFrame -= 4;
            else if( speed.n < 0 )
                self->nextToRenderFrame += 4;

            self->filled = -1;
            lastDuration = INT64_C(0);

            // Write where the reader will read next
            self->writeBuffer = self->readBuffer;
        }

        if( wasRenderOneFrame ) {
            // Draw the frame at the next opportunity
            self->readBuffer = writeBuffer;
            self->drawOneFrame++;
            g_timeout_add_full( G_PRIORITY_DEFAULT, 0, (GSourceFunc) playSingleFrame, self, NULL );

            // Otherwise, loop around and do it again
        }
        else {
            self->filled++;

            if( lastDuration < INT64_C(0) )
                lastDuration *= INT64_C(-1);

            if( speed.n > 0 ) {
                while( get_frame_time( &self->frameRate, ++self->nextToRenderFrame ) < endTime + lastDuration );
            }
            else if( speed.n < 0 ) {
                while( get_frame_time( &self->frameRate, --self->nextToRenderFrame ) > endTime - lastDuration );
            }

            //printf( "nextFrame: %d, lastDuration: %ld, endTime: %ld\n", self->nextToRenderFrame, lastDuration, endTime );

            target->nextTime = get_frame_time( &self->frameRate, self->nextToRenderFrame );
        }

        g_mutex_unlock( self->frameReadMutex );

/*            std::stringstream filename;
        filename << "rgba" << i++ << ".exr";

        Header header( 720, 480, 40.0f / 33.0f );

        RgbaOutputFile file( filename.str().c_str(), header, WRITE_RGBA );
        file.setFrameBuffer( &array[0][0], 1, 720 );
        file.writePixels( 480 );

        puts( filename.str().c_str() );*/
    }

    return NULL;
}

EXPORT widget_gl_context *
widget_gl_new() {
    init_half();

    if( !g_thread_supported() )
        g_thread_init( NULL );

    widget_gl_context *self = g_malloc0( sizeof(widget_gl_context) );

    self->frameRate.n = 24000;
    self->frameRate.d = 1001u;

    self->softMode = true;
    self->hardModeDisable = false;
    self->bufferCount = SOFT_MODE_BUFFERS;

    g_static_rw_lock_init( &self->frame_read_rwlock );
    self->frameReadMutex = g_mutex_new();
    self->frameReadCond = g_cond_new();
    self->nextToRenderFrame = 0;
    self->filled = self->bufferCount - 1;
    self->readBuffer = self->bufferCount - 1;
    self->writeBuffer = self->bufferCount - 1;
    self->firstFrame = 0;
    self->lastFrame = INT_MAX;
    self->pixelAspectRatio = 40.0f / 33.0f;
    self->quit = false;
    self->softTextureId = 0;
    self->hardTextureId = 0;
    self->checkerTextureId = 0;
    self->renderOneFrame = true;
    self->lastHardFrame = -1;
    self->clock_callback_handle = NULL;
    self->checkerColors[0].r = self->checkerColors[0].g = self->checkerColors[0].b = 128;
    self->checkerColors[1].r = self->checkerColors[1].g = self->checkerColors[1].b = 192;

    for( int i = 0; i < SOFT_MODE_BUFFERS; i++ ) {
        self->softTargets[i].frameData = NULL;
        box2i_set_empty( &self->softTargets[i].fullDataWindow );
    }

    self->renderThread = g_thread_create( (GThreadFunc) playbackThread, self, TRUE, NULL );

    self->gamma_ramp = (uint8_t *) g_malloc( HALF_COUNT );
    self->rendering_intent = 0.0f;
    widget_gl_set_rendering_intent( self, 1.25f );

    return self;
}

EXPORT void
widget_gl_free( widget_gl_context *self ) {
    // Stop the render thread
    if( self->frameReadMutex != NULL ) {
        g_mutex_lock( self->frameReadMutex );
        self->quit = true;
        g_cond_signal( self->frameReadCond );
        g_mutex_unlock( self->frameReadMutex );
    }
    else
        self->quit = true;

    if( self->renderThread != NULL )
        g_thread_join( self->renderThread );

    g_static_rw_lock_free( &self->frame_read_rwlock );

    if( self->clock.funcs && self->clock_callback_handle )
        self->clock.funcs->unregister_callback( self->clock.obj, self->clock_callback_handle );

    self->clock.funcs = NULL;
    self->frameSource = NULL;

    if( self->frameReadMutex != NULL ) {
        g_mutex_free( self->frameReadMutex );
        self->frameReadMutex = NULL;
    }

    if( self->frameReadCond != NULL ) {
        g_cond_free( self->frameReadCond );
        self->frameReadCond = NULL;
    }

    g_free( self );
}

static void
widget_gl_initialize( widget_gl_context *self ) {
    self->hardModeSupported =
        GLEW_VERSION_2_1 &&
        GLEW_ATI_texture_float &&
        GLEW_ARB_texture_rectangle &&
        GLEW_EXT_framebuffer_object &&
        GLEW_ARB_half_float_pixel;

    if( !self->hardModeSupported )
        g_warning( "Hardware mode not supported on this hardware" );

    self->softMode = !self->hardModeSupported || self->hardModeDisable;

    v2i frameSize;
    box2i_get_size( &self->displayWindow, &frameSize );

    if( !self->softTextureId && self->softMode ) {
        glGenTextures( 1, &self->softTextureId );
        glBindTexture( GL_TEXTURE_RECTANGLE_ARB, self->softTextureId );

        glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

        glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, frameSize.x, frameSize.y,
            0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );

        glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );
    }

    if( !self->checkerTextureId ) {
        rgb8 checker[4] = { self->checkerColors[0], self->checkerColors[1],
                            self->checkerColors[1], self->checkerColors[0] };

        glGenTextures( 1, &self->checkerTextureId );
        glBindTexture( GL_TEXTURE_2D, self->checkerTextureId );

        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );

        glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
        glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, checker );
        glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );

        glBindTexture( GL_TEXTURE_2D, 0 );
    }
}

static void
widget_gl_softLoadTexture( widget_gl_context *self ) {
    if( self->softTargets[self->readBuffer].frameData == NULL ) {
        box2i_set_empty( &self->currentDataWindow );
        return;
    }

    // Load texture
    v2i frameSize;
    box2i_get_size( &self->displayWindow, &frameSize );

    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, self->softTextureId );
    glTexSubImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, frameSize.x, frameSize.y,
        GL_RGBA, GL_UNSIGNED_BYTE, &self->softTargets[self->readBuffer].frameData[0] );
    gl_checkError();

    self->currentDataWindow = self->softTargets[self->readBuffer].currentDataWindow;
}

static void
widget_gl_hardLoadTexture( widget_gl_context *self ) {
    // This is being done on the main app thread
    // TODO: Schedule multiple frames through OpenGL at once
    // (the driver will work out the sync issues); perhaps do
    // a pre-roll at the beginning to get more frames in the
    // buffer?
    if( self->hardTextureId ) {
        glDeleteTextures( 1, &self->hardTextureId );
        self->hardTextureId = 0;
    }

    // Since this is on the main thread, we don't
    // worry about locking
    if( self->frameSource == NULL ) {
        box2i_set_empty( &self->currentDataWindow );
        return;
    }

    // Pull out the next frame
    int frameIndex = self->nextToRenderFrame;

    // Clamp to the set first/last frames
    if( frameIndex > self->lastFrame )
        frameIndex = self->lastFrame;
    else if( frameIndex < self->firstFrame )
        frameIndex = self->firstFrame;

    rgba_frame_gl frame = {
        .full_window = self->displayWindow,
        .current_window = self->displayWindow
    };

    video_get_frame_gl( self->frameSource, frameIndex, &frame );

    self->hardTextureId = frame.texture;
    self->lastHardFrame = frameIndex;
    self->currentDataWindow = frame.current_window;
}

static const char *vertex_shader_text =
"#version 120\n"
"#extension GL_ARB_texture_rectangle : require\n"
"uniform ivec2 widget_size;\n"
"uniform ivec2 frame_size;\n"
"uniform float pixel_aspect_ratio;\n"
"attribute vec2 position;\n"
"varying vec2 checkerboard_coord;\n"
"varying vec2 frame_coord;\n"
"\n"
"void main() {\n"
"    gl_Position = vec4(position * vec2(2.0, 2.0) + vec2(-1.0, -1.0), 0.0, 1.0);\n"
"    checkerboard_coord = position * vec2(widget_size) * (1.0 / 32.0);\n"
"\n"
"    float widget_ar = float(widget_size.x) / float(widget_size.y);\n"
"    float frame_ar = float(frame_size.x) * pixel_aspect_ratio / float(frame_size.y);\n"
"    float scale;\n"
"\n"
"    if( widget_ar > frame_ar ) {\n"
"        // Constraining dimension is Y\n"
"        scale = float(frame_size.y) / float(widget_size.y);\n"
"    }\n"
"    else {\n"
"        scale = float(frame_size.x) * pixel_aspect_ratio / float(widget_size.x);\n"
"    }\n"
"\n"
"    frame_coord =\n"
"        // Find the center of the frame and reverse to a widget corner\n"
"        (vec2(frame_size) - widget_size * scale * vec2(1.0 / pixel_aspect_ratio, -1.0)) * 0.5\n"
"\n"
"        // Then take us to each of the four corners\n"
"        + position * widget_size * scale * vec2(1.0 / pixel_aspect_ratio, -1.0);\n"
"}\n";

static const char *gamma_shader_text =
"#version 120\n"
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2D checkerboard_texture;\n"
"uniform sampler2DRect frame_texture;\n"
"uniform bool gamma_correction;\n"
"varying vec2 checkerboard_coord;\n"
"varying vec2 frame_coord;\n"
"const float transition = 0.0031308f;\n"
"const vec4 a = vec4(0.055);\n"
"\n"
"vec4 color_over( vec4 color_a, vec4 color_b ) {\n"
"    vec4 result;\n"
"    float alpha_a = color_a.a * (1.0f - color_b.a);\n"
"    float alpha_b = color_b.a;\n"
"\n"
"    result.a = alpha_a + alpha_b;\n"
"\n"
"    if( result.a != 0.0 )\n"
"        result.rgb = (color_a.rgb * alpha_a + color_b.rgb * alpha_b) / result.a;\n"
"    else\n"
"        result.rgb = vec3(0.0, 0.0, 0.0);\n"
"\n"
"    return result;\n"
"}\n"
"\n"
"void main() {\n"
"    // GL equivalent of linear_to_sRGB in gammatab.c\n"
"    vec4 checker_color = texture2D(checkerboard_texture, checkerboard_coord);\n"
"    vec4 frame_color = texture2DRect(frame_texture, frame_coord);\n"
"\n"
"    if( gamma_correction ) {\n"
"        frame_color = mix(\n"
"            frame_color * 12.92f,\n"
"            (1.0f + a) * pow(frame_color, vec4(1.0/2.4)) - a,\n"
"            step(transition, frame_color));\n"
"    }\n"
"\n"
"    gl_FragColor = color_over(checker_color, frame_color);\n"
"}\n";

typedef struct {
    GLuint program;
    GLint checkerboard_texture_uniform, frame_texture_uniform, gamma_correction_uniform;
    GLint widget_size_uniform, frame_size_uniform, pixel_aspect_ratio_uniform;
    GLint position_attrib;
    GLuint vertex_buffer;
} gl_shader_state;

static void destroy_shader( gl_shader_state *shader ) {
    // We assume that we're in the right GL context
    glDeleteProgram( shader->program );
    glDeleteBuffers( 1, &shader->vertex_buffer );
    g_free( shader );
}

/*
    Function: widget_gl_draw
    Paint the widget with the current GL context.

    Parameters:
    self - The widget_gl_context.
    widget_size - The current size of the widget.

    Remarks:
    Call this function inside your widget's expose function after
    setting up the GL context. This function will set up everything else.
*/
EXPORT void
widget_gl_draw( widget_gl_context *self, v2i widget_size ) {
    widget_gl_initialize( self );

    if( self->softMode ) {
        widget_gl_softLoadTexture( self );
    }
    else if( self->renderOneFrame ) {
        widget_gl_hardLoadTexture( self );
        self->renderOneFrame = false;
    }

    if( !self->softMode && !self->renderOneFrame && self->lastHardFrame != self->nextToRenderFrame )
        widget_gl_hardLoadTexture( self );

    GQuark shader_quark = g_quark_from_static_string( "cprocess::widget_gl::widget_gl_gamma_program" );

    void *context = getCurrentGLContext();
    gl_shader_state *shader = (gl_shader_state *) g_dataset_id_get_data( context, shader_quark );

    if( !shader ) {
        // Time to create the program for this context
        shader = g_new0( gl_shader_state, 1 );

        GLuint shaders[2];
        shaders[0] = gl_compile_shader( GL_VERTEX_SHADER, vertex_shader_text,
            "Widget GL vertex shader" );
        shaders[1] = gl_compile_shader( GL_FRAGMENT_SHADER, gamma_shader_text,
            "Widget GL gamma fragment shader" );

        shader->program = gl_link_program( shaders, 2, "Widget GL program" );

        glDeleteShader( shaders[0] );
        glDeleteShader( shaders[1] );

        shader->checkerboard_texture_uniform = glGetUniformLocation( shader->program, "checkerboard_texture" );
        shader->frame_texture_uniform = glGetUniformLocation( shader->program, "frame_texture" );
        shader->gamma_correction_uniform = glGetUniformLocation( shader->program, "gamma_correction" );
        shader->widget_size_uniform = glGetUniformLocation( shader->program, "widget_size" );
        shader->frame_size_uniform = glGetUniformLocation( shader->program, "frame_size" );
        shader->pixel_aspect_ratio_uniform = glGetUniformLocation( shader->program, "pixel_aspect_ratio" );
        shader->position_attrib = glGetAttribLocation( shader->program, "position" );

        // Set up a static buffer with four corners for us to work with
        glGenBuffers( 1, &shader->vertex_buffer );

        v2f positions[4] = {
            { 0.0f, 0.0f },
            { 1.0f, 0.0f },
            { 1.0f, 1.0f },
            { 0.0f, 1.0f }
        };

        glBindBuffer( GL_ARRAY_BUFFER, shader->vertex_buffer );
        glBufferData( GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );

        g_dataset_id_set_data_full( context, shader_quark, shader, (GDestroyNotify) destroy_shader );
    }

    v2i frame_size;
    box2i_get_size( &self->displayWindow, &frame_size );

    // Set up for drawing
    glViewport( 0, 0, widget_size.x, widget_size.y );

    glUseProgram( shader->program );

    glBindTexture( GL_TEXTURE_2D, self->checkerTextureId );
    glEnable( GL_TEXTURE_2D );
    glActiveTexture( GL_TEXTURE1 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, self->softMode ? self->softTextureId : self->hardTextureId );
    glEnable( GL_TEXTURE_RECTANGLE_ARB );

    glBindBuffer( GL_ARRAY_BUFFER, shader->vertex_buffer );
    glEnableVertexAttribArray( shader->position_attrib );
    glVertexAttribPointer( shader->position_attrib, 2, GL_FLOAT, GL_FALSE, 0, 0 );

    // Set uniforms
    glUniform1i( shader->checkerboard_texture_uniform, 0 );
    glUniform1i( shader->frame_texture_uniform, 1 );
    glUniform1i( shader->gamma_correction_uniform, self->softMode ? 0 : 1 );
    glUniform2iv( shader->widget_size_uniform, 1, &widget_size.x );
    glUniform2iv( shader->frame_size_uniform, 1, &frame_size.x );
    glUniform1f( shader->pixel_aspect_ratio_uniform, self->pixelAspectRatio );

    // Draw
    glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );

    // Clean up
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glDisableVertexAttribArray( shader->position_attrib );

    glDisable( GL_TEXTURE_RECTANGLE_ARB );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );

    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, 0 );
    glDisable( GL_TEXTURE_2D );

    glUseProgram( 0 );
}

/*
    Function: widget_gl_set_invalidate_func
    Set the function to be called when the widget needs to be repainted.

    Parameters:
    self - The widget_gl_context.
    func - The function to be called.
    closure - Argument to pass to func.

    Remarks:
    The proper thing to do when *func* is called is probably invalidate
    the widget.
*/
EXPORT void
widget_gl_set_invalidate_func( widget_gl_context *self, invalidate_func func, void *closure ) {
    self->invalidate_func = func;
    self->invalidate_closure = closure;
}

EXPORT gboolean
widget_gl_get_hard_mode_enabled( widget_gl_context *self ) {
    return !self->hardModeDisable && self->hardModeSupported;
}

/*
    Function: widget_gl_get_hard_mode_supported
    Determine whether hard mode is supported.

    Parameters:
    self - The widget_gl_context.

    Remarks:
    This will return false until widget_gl_draw has been called for the first time.
*/
EXPORT gboolean
widget_gl_get_hard_mode_supported( widget_gl_context *self ) {
    return self->hardModeSupported;
}

EXPORT void
widget_gl_hard_mode_enable( widget_gl_context *self, gboolean enable ) {
    self->hardModeDisable = !enable;
}

EXPORT void
widget_gl_get_display_window( widget_gl_context *self, box2i *display_window ) {
    *display_window = self->displayWindow;
}

EXPORT void
widget_gl_set_display_window( widget_gl_context *self, box2i *display_window ) {
    g_mutex_lock( self->frameReadMutex );
    self->displayWindow = *display_window;
    g_mutex_unlock( self->frameReadMutex );
}

EXPORT void
widget_gl_set_video_source( widget_gl_context *self, video_source *source ) {
    // Since widget_gl doesn't dealloc/deref its source, this lock may
    // seem almost superfluous, but it serves as a "write barrier" for anyone
    // setting the source: they shouldn't deallocate the old pointer until this
    // call returns.
    g_static_rw_lock_writer_lock( &self->frame_read_rwlock );
    self->frameSource = source;
    g_static_rw_lock_writer_unlock( &self->frame_read_rwlock );
}

static void _clock_callback( widget_gl_context *self, rational *speed, int64_t time );

EXPORT void
widget_gl_set_presentation_clock( widget_gl_context *self, presentation_clock *clock ) {
    g_mutex_lock( self->frameReadMutex );

    if( self->clock.funcs && self->clock_callback_handle ) {
        self->clock.funcs->unregister_callback( self->clock.obj, self->clock_callback_handle );
        self->clock_callback_handle = NULL;
    }

    self->clock = *clock;

    if( self->clock.funcs && self->clock.funcs->register_callback ) {
        self->clock_callback_handle = self->clock.funcs->register_callback( self->clock.obj,
            (clock_callback_func) _clock_callback, self, NULL );
    }

    g_mutex_unlock( self->frameReadMutex );
}

static void
widget_gl_play( widget_gl_context *self ) {
    if( self->timeoutSourceID != 0 ) {
        g_source_remove( self->timeoutSourceID );
        self->timeoutSourceID = 0;
    }

    // Fire up the production and playback threads from scratch
    g_mutex_lock( self->frameReadMutex );
    int64_t stopTime = self->clock.funcs->getPresentationTime( self->clock.obj );
    self->nextToRenderFrame = get_time_frame( &self->frameRate, stopTime );
    self->filled = -2;
    g_cond_signal( self->frameReadCond );
    g_mutex_unlock( self->frameReadMutex );

    playSingleFrame( self );
}

static void
widget_gl_stop( widget_gl_context *self ) {
    if( self->timeoutSourceID != 0 ) {
        g_source_remove( self->timeoutSourceID );
        self->timeoutSourceID = 0;
    }

    // Have the production thread play one more frame, then stop
    g_mutex_lock( self->frameReadMutex );
    self->filled = 3;

    int64_t stopTime = self->clock.funcs->getPresentationTime( self->clock.obj );

    self->renderOneFrame = true;
    self->nextToRenderFrame = get_time_frame( &self->frameRate, stopTime );
    g_cond_signal( self->frameReadCond );
    g_mutex_unlock( self->frameReadMutex );

    if( !self->softMode ) {
        // We have to play the one frame ourselves
        playSingleFrame( self );
    }
}

static void
_clock_callback( widget_gl_context *self, rational *speed, int64_t time ) {
    // All we have to do here is call the old play/stop interface
    if( speed->n == 0 )
        widget_gl_stop( self );
    else
        widget_gl_play( self );
}

EXPORT float
widget_gl_get_pixel_aspect_ratio( widget_gl_context *self ) {
    return self->pixelAspectRatio;
}

EXPORT void
widget_gl_set_pixel_aspect_ratio( widget_gl_context *self, float pixel_aspect_ratio ) {
    self->pixelAspectRatio = pixel_aspect_ratio;

    if( self->invalidate_func )
        self->invalidate_func( self->invalidate_closure );
}

/*
    Function: widget_gl_get_rendering_intent
    Gets the current rendering intent (additional gamma).

    Parameters:
    self - The widget_gl_context.

    Remarks:
    The default rendering intent is 1.25.
*/
EXPORT float
widget_gl_get_rendering_intent( widget_gl_context *self ) {
    return self->rendering_intent;
}

/*
    Function: widget_gl_get_rendering_intent
    Sets the rendering intent (additional gamma).

    Parameters:
    self - The widget_gl_context.
    rendering_intent - The new rendering intent.
*/
EXPORT void
widget_gl_set_rendering_intent( widget_gl_context *self, float rendering_intent ) {
    if( rendering_intent == self->rendering_intent )
        return;

    self->rendering_intent = rendering_intent;

    half *h = g_malloc( sizeof(half) * HALF_COUNT );
    float *f = g_malloc( sizeof(float) * HALF_COUNT );

    for( int i = 0; i < HALF_COUNT; i++ )
        h[i] = (half) i;

    half_convert_to_float( f, h, HALF_COUNT );
    g_free( h );

    for( int i = 0; i < HALF_COUNT; i++ )
        self->gamma_ramp[i] = (uint8_t) lrint( clampf( powf( f[i], rendering_intent ) * 255.0f, 0.0f, 255.0f ) );

    g_free( f );
}

