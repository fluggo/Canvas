#include "widget_gl.h"

static uint8_t gamma45[65536];
#define    SOFT_MODE_BUFFERS    4
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

    video_source frameSource;
    presentation_clock clock;
    GMutex *frameReadMutex;
    GCond *frameReadCond;
    int nextToRenderFrame;
    int readBuffer, writeBuffer, filled;
    rational frameRate;
    guint timeoutSourceID;
    box2i displayWindow, currentDataWindow;
    int firstFrame, lastFrame;
    float pixelAspectRatio;
    bool renderOneFrame, drawOneFrame;
    int lastHardFrame;

    rgb8 checkerColors[2];

    // True: render in software, false, render in hardware
    bool softMode;
    bool hardModeDisable, hardModeSupported;

    SoftFrameTarget softTargets[SOFT_MODE_BUFFERS];
    GLuint softTextureId, hardTextureId, checkerTextureId;
    GLhandleARB hardGammaShader, hardGammaProgram;

    // Number of buffers available
    int bufferCount;

    float rate;
    bool quit;
    GThread *renderThread;
};

static gboolean
playSingleFrame( widget_gl_context *self ) {
    if( self->quit )
        return FALSE;

    if( self->softMode ) {
        if( self->filled > 0 || self->drawOneFrame ) {
            g_mutex_lock( self->frameReadMutex );
            int filled = self->filled;

            if( !self->drawOneFrame )
                self->readBuffer = (self->readBuffer + 1) % self->bufferCount;

            int64_t nextPresentationTime = self->softTargets[self->readBuffer].nextTime;
            g_mutex_unlock( self->frameReadMutex );

            if( filled != 0 || self->drawOneFrame ) {
                if( self->invalidate_func )
                    self->invalidate_func( self->invalidate_closure );

                //printf( "Painted %d from %d...\n", getTimeFrame( &self->frameRate, self->targets[self->readBuffer].time ), self->readBuffer );

                if( self->drawOneFrame ) {
                    // We're done here, go back to sleep
                    self->drawOneFrame = false;
                }
                else {
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

            int64_t nextPresentationTime = getFrameTime( &self->frameRate, self->nextToRenderFrame );

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

    box2i_getSize( box1, &size1 );
    box2i_getSize( box2, &size2 );

    return size1.x == size2.x && size1.y == size2.y;
}

static gpointer
playbackThread( widget_gl_context *self ) {
    rgba_f16_frame frame = { NULL };
    box2i_setEmpty( &frame.fullDataWindow );

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

        box2i_getSize( &self->displayWindow, &frameSize );
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
        if( box2i_isEmpty( &target->fullDataWindow ) ||
            !box2i_equalSize( &self->displayWindow, &target->fullDataWindow ) ) {

            g_free( target->frameData );
            target->frameData = g_malloc( frameSize.y * frameSize.x * sizeof(rgba_u8) );
            target->stride = frameSize.x;
        }

        // If our target array is the wrong size, reallocate it now
        if( box2i_isEmpty( &frame.fullDataWindow ) ||
            !box2i_equalSize( &self->displayWindow, &frame.fullDataWindow ) ) {

            g_free( frame.frameData );
            frame.frameData = g_malloc( frameSize.y * frameSize.x * sizeof(rgba_f16) );
            frame.stride = frameSize.x;
        }

        // The frame could shift without changing size, so we set this here just in case
        target->fullDataWindow = self->displayWindow;
        frame.fullDataWindow = self->displayWindow;

        VideoFrameSourceFuncs *funcs = self->frameSource.funcs;

        g_mutex_unlock( self->frameReadMutex );

//        printf( "Start rendering %d into %d...\n", nextFrame, writeBuffer );

        // Pull the frame data from the chain
        frame.currentDataWindow = target->fullDataWindow;

        if( funcs != NULL ) {
            getFrame_f16( &self->frameSource, nextFrame, &frame );
        }
        else {
            // No result
            box2i_setEmpty( &frame.currentDataWindow );
        }

        target->currentDataWindow = frame.currentDataWindow;

        // Convert the results to floating-point
        for( int y = frame.currentDataWindow.min.y; y <= frame.currentDataWindow.max.y; y++ ) {
            rgba_u8 *targetData = &target->frameData[(y - target->fullDataWindow.min.y) * target->stride];
            rgba_f16 *sourceData = &frame.frameData[(y - frame.fullDataWindow.min.y) * frame.stride];

            for( int x = frame.currentDataWindow.min.x; x <= frame.currentDataWindow.max.x; x++ ) {
                targetData[x].r = gamma45[sourceData[x].r];
                targetData[x].g = gamma45[sourceData[x].g];
                targetData[x].b = gamma45[sourceData[x].b];
                targetData[x].a = gamma45[sourceData[x].a];
            }
        }

        //usleep( 100000 );

        self->softTargets[writeBuffer].time = getFrameTime( &self->frameRate, nextFrame );
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

        if( self->renderOneFrame ) {
            // We're done here, draw the frame at the next opportunity
            self->readBuffer = writeBuffer;
            self->renderOneFrame = false;
            self->drawOneFrame = true;
            g_timeout_add_full( G_PRIORITY_DEFAULT, 0, (GSourceFunc) playSingleFrame, self, NULL );
        }
        else {
            self->filled++;

            if( lastDuration < INT64_C(0) )
                lastDuration *= INT64_C(-1);

            if( speed.n > 0 ) {
                while( getFrameTime( &self->frameRate, ++self->nextToRenderFrame ) < endTime + lastDuration );
            }
            else if( speed.n < 0 ) {
                while( getFrameTime( &self->frameRate, --self->nextToRenderFrame ) > endTime - lastDuration );
            }

            //printf( "nextFrame: %d, lastDuration: %ld, endTime: %ld\n", self->nextToRenderFrame, lastDuration, endTime );

            target->nextTime = getFrameTime( &self->frameRate, self->nextToRenderFrame );
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

static inline float gamma45Func( float input ) {
    return clampf( powf( input, 0.45f ) * 255.0f, 0.0f, 255.0f );
}

EXPORT widget_gl_context *
widget_gl_new() {
    init_half();

    if( !g_thread_supported() )
        g_thread_init( NULL );

    static bool __gamma_init = false;

    if( !__gamma_init ) {
        // Fill in the 0.45 gamma table
        half *h = g_malloc( sizeof(half) * 65536 );
        float *f = g_malloc( sizeof(float) * 65536 );

        for( int i = 0; i < 65536; i++ )
            h[i] = (half) i;

        half_convert_to_float( h, f, 65536 );
        g_free( h );

        for( int i = 0; i < 65536; i++ )
            gamma45[i] = (uint8_t) gamma45Func( f[i] );

        g_free( f );

        __gamma_init = true;
    }

    widget_gl_context *self = g_malloc0( sizeof(widget_gl_context) );

    self->frameRate.n = 24000;
    self->frameRate.d = 1001u;

    self->softMode = true;
    self->hardModeDisable = false;
    self->bufferCount = SOFT_MODE_BUFFERS;

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
    self->hardGammaShader = 0;
    self->renderOneFrame = true;
    self->lastHardFrame = -1;
    self->checkerColors[0].r = self->checkerColors[0].g = self->checkerColors[0].b = 128;
    self->checkerColors[1].r = self->checkerColors[1].g = self->checkerColors[1].b = 192;

    for( int i = 0; i < SOFT_MODE_BUFFERS; i++ ) {
        self->softTargets[i].frameData = NULL;
        box2i_setEmpty( &self->softTargets[i].fullDataWindow );
    }

    self->renderThread = g_thread_create( (GThreadFunc) playbackThread, self, TRUE, NULL );

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

    self->clock.funcs = NULL;
    self->frameSource.funcs = NULL;

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
    static bool __glewInit = false;

    if( !__glewInit ) {
        glewInit();
        __glewInit = true;

        self->hardModeSupported =
            GLEW_ATI_texture_float &&
            GLEW_ARB_texture_rectangle &&
            GLEW_ARB_fragment_shader &&
            GLEW_EXT_framebuffer_object &&
            GLEW_ARB_half_float_pixel;
    }

    self->softMode = !self->hardModeSupported || self->hardModeDisable;

    v2i frameSize;
    box2i_getSize( &self->displayWindow, &frameSize );

    if( !self->softTextureId && self->softMode ) {
        glGenTextures( 1, &self->softTextureId );
        glBindTexture( GL_TEXTURE_RECTANGLE_ARB, self->softTextureId );

        glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
        glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

        glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, frameSize.x, frameSize.y,
            0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
    }

    if( !self->checkerTextureId ) {
        rgb8 checker[4] = { self->checkerColors[0], self->checkerColors[1],
                            self->checkerColors[1], self->checkerColors[0] };

        glGenTextures( 1, &self->checkerTextureId );
        glBindTexture( GL_TEXTURE_2D, self->checkerTextureId );

        glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );

        glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, checker );
    }
}

static void
widget_gl_softLoadTexture( widget_gl_context *self ) {
    if( self->softTargets[self->readBuffer].frameData == NULL ) {
        box2i_setEmpty( &self->currentDataWindow );
        return;
    }

    // Load texture
    v2i frameSize;
    box2i_getSize( &self->displayWindow, &frameSize );

    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, self->softTextureId );
    glTexSubImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, frameSize.x, frameSize.y,
        GL_RGBA, GL_UNSIGNED_BYTE, &self->softTargets[self->readBuffer].frameData[0] );
    gl_checkError();

    self->currentDataWindow = self->softTargets[self->readBuffer].currentDataWindow;
}

static void
widget_gl_hardLoadTexture( widget_gl_context *self ) {
    // This is being done on the main app thread
    if( self->hardTextureId ) {
        glDeleteTextures( 1, &self->hardTextureId );
        self->hardTextureId = 0;
    }

    if( self->frameSource.obj == NULL ) {
        box2i_setEmpty( &self->currentDataWindow );
        return;
    }

    // Pull out the next frame
    int frameIndex = self->nextToRenderFrame;

    // Clamp to the set first/last frames
    if( frameIndex > self->lastFrame )
        frameIndex = self->lastFrame;
    else if( frameIndex < self->firstFrame )
        frameIndex = self->firstFrame;

    rgba_gl_frame frame = {
        .fullDataWindow = self->displayWindow,
        .currentDataWindow = self->displayWindow
    };

    getFrame_gl( &self->frameSource, frameIndex, &frame );

    self->hardTextureId = frame.texture;
    self->lastHardFrame = frameIndex;
    self->currentDataWindow = frame.currentDataWindow;
}

static const char *gammaShader =
"#version 110\n"
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect tex;"
//"out vec4 gl_FragColor;"
""
"void main() {"
"    vec4 color = texture2DRect( tex, gl_TexCoord[0].st );"
"    gl_FragColor.rgba = pow( color.rgba, vec4(0.45, 0.45, 0.45, 0.45) );"
"}";

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

    // Set ourselves up with the correct aspect ratio for the space
    v2i frameSize;
    box2i_getSize( &self->displayWindow, &frameSize );

    float width = frameSize.x * self->pixelAspectRatio;
    float height = frameSize.y;

    if( width > widget_size.x ) {
        height *= widget_size.x / width;
        width = widget_size.x;
    }

    if( height > widget_size.y ) {
        width *= widget_size.y / height;
        height = widget_size.y;
    }

    // Upper-left
    float x = floor( widget_size.x * 0.5f - width * 0.5f );
    float y = floor( widget_size.y * 0.5f - height * 0.5f );

    // Set up for drawing
    glLoadIdentity();
    glViewport( 0, 0, widget_size.x, widget_size.y );
    glOrtho( 0, widget_size.x, widget_size.y, 0, -1, 1 );

    glClearColor( 0.3f, 0.3f, 0.3f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );

    // Render the checker onto the field
    glBindTexture( GL_TEXTURE_2D, self->checkerTextureId );
    glEnable( GL_TEXTURE_2D );

    glBegin( GL_QUADS );
    glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
    glTexCoord2f( self->displayWindow.min.x / 64.0f, self->displayWindow.min.y / 64.0f );
    glVertex2f( x, y );
    glTexCoord2f( self->displayWindow.max.x / 64.0f, self->displayWindow.min.y / 64.0f );
    glVertex2f( x + width, y );
    glTexCoord2f( self->displayWindow.max.x / 64.0f, self->displayWindow.max.y / 64.0f );
    glVertex2f( x + width, y + height );
    glTexCoord2f( self->displayWindow.min.x / 64.0f, self->displayWindow.max.y / 64.0f );
    glVertex2f( x, y + height );
    glEnd();

    glDisable( GL_TEXTURE_2D );

    if( box2i_isEmpty( &self->currentDataWindow ) )
        return;

    if( !self->softMode ) {
        if( !self->hardGammaShader )
            gl_buildShader( gammaShader, &self->hardGammaShader, &self->hardGammaProgram );

        glUseProgramObjectARB( self->hardGammaProgram );
    }

    // Find the corners of the defined window
    x += (self->currentDataWindow.min.x - self->displayWindow.min.x) * width / frameSize.x;
    y += (self->currentDataWindow.min.y - self->displayWindow.min.y) * height / frameSize.y;
    width *= (float)(self->currentDataWindow.max.x - self->currentDataWindow.min.x + 1) / (float)(frameSize.x);
    height *= (float)(self->currentDataWindow.max.y - self->currentDataWindow.min.y + 1) / (float)(frameSize.y);

    // Render texture onto quad
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, self->softMode ? self->softTextureId : self->hardTextureId );
    glEnable( GL_TEXTURE_RECTANGLE_ARB );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    glBegin( GL_QUADS );
    glTexCoord2i( self->currentDataWindow.min.x - self->displayWindow.min.x, self->currentDataWindow.min.y - self->displayWindow.min.y );
    glVertex2f( x, y );
    glTexCoord2i( self->currentDataWindow.max.x - self->displayWindow.min.x + 1, self->currentDataWindow.min.y - self->displayWindow.min.y );
    glVertex2f( x + width, y );
    glTexCoord2i( self->currentDataWindow.max.x - self->displayWindow.min.x + 1, self->currentDataWindow.max.y - self->displayWindow.min.y + 1 );
    glVertex2f( x + width, y + height );
    glTexCoord2i( self->currentDataWindow.min.x - self->displayWindow.min.x, self->currentDataWindow.max.y - self->displayWindow.min.y + 1 );
    glVertex2f( x, y + height );
    glEnd();

    glDisable( GL_TEXTURE_RECTANGLE_ARB );
    glDisable( GL_BLEND );

    if( !self->softMode ) {
        glUseProgramObjectARB( 0 );
    }
}

EXPORT void
widget_gl_set_invalidate_func( widget_gl_context *self, invalidate_func func, void *closure ) {
    self->invalidate_func = func;
    self->invalidate_closure = closure;
}

EXPORT gboolean
widget_gl_get_hard_mode_enabled( widget_gl_context *self ) {
    return !self->hardModeDisable && self->hardModeSupported;
}

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
    g_mutex_lock( self->frameReadMutex );
    self->frameSource = *source;
    g_mutex_unlock( self->frameReadMutex );
}

EXPORT void
widget_gl_set_presentation_clock( widget_gl_context *self, presentation_clock *clock ) {
    g_mutex_lock( self->frameReadMutex );
    self->clock = *clock;
    g_mutex_unlock( self->frameReadMutex );
}

EXPORT void
widget_gl_play( widget_gl_context *self ) {
    if( self->timeoutSourceID != 0 ) {
        g_source_remove( self->timeoutSourceID );
        self->timeoutSourceID = 0;
    }

    // Fire up the production and playback threads from scratch
    g_mutex_lock( self->frameReadMutex );
    int64_t stopTime = self->clock.funcs->getPresentationTime( self->clock.obj );
    self->nextToRenderFrame = getTimeFrame( &self->frameRate, stopTime );
    self->filled = -2;
    g_cond_signal( self->frameReadCond );
    g_mutex_unlock( self->frameReadMutex );

    playSingleFrame( self );
}

EXPORT void
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
    self->nextToRenderFrame = getTimeFrame( &self->frameRate, stopTime );
    g_cond_signal( self->frameReadCond );
    g_mutex_unlock( self->frameReadMutex );

    if( !self->softMode ) {
        // We have to play the one frame ourselves
        playSingleFrame( self );
    }
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

