
#define __STDC_CONSTANT_MACROS
#include <stdint.h>

#include <pygobject.h>
#include <pygtk/pygtk.h>
#include <Python.h>
#include <structmember.h>
#include "framework.h"
#include "clock.h"

#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <gdk/gdkkeysyms.h>
#include <GL/glew.h>
#include <GL/gl.h>

static inline float clamppowf( float x, float y ) {
    if( x < 0.0f )
        return 0.0f;

    if( x > 1.0f )
        return 1.0f;

    return powf( x, y );
}

static inline float clampf( float x, float min, float max ) {
    return (x < min) ? min : ((x > max) ? max : x);
}

static inline float gamma45Func( float input ) {
    return clampf( powf( input, 0.45f ) * 255.0f, 0.0f, 255.0f );
}

static uint8_t gamma45[65536];

static void checkGLError() {
    int error = glGetError();

    switch( error ) {
        case GL_NO_ERROR:
            return;

        case GL_INVALID_OPERATION:
            puts( "Invalid operation" );
            return;

        case GL_INVALID_VALUE:
            puts( "Invalid value" );
            return;

        case GL_INVALID_ENUM:
            puts( "Invalid enum" );
            return;

        default:
            puts( "Other GL error" );
            return;
    }
}

bool takeVideoSource( PyObject *source, VideoSourceHolder *holder ) {
    Py_CLEAR( holder->source );
    Py_CLEAR( holder->csource );
    holder->funcs = NULL;

    if( source == NULL || source == Py_None )
        return true;

    Py_INCREF( source );
    holder->source = source;
    holder->csource = PyObject_GetAttrString( source, "_videoFrameSourceFuncs" );

    if( holder->csource == NULL ) {
        Py_CLEAR( holder->source );
        PyErr_SetString( PyExc_Exception, "The source didn't have an acceptable _videoFrameSourceFuncs attribute." );
        return false;
    }

    holder->funcs = (VideoFrameSourceFuncs*) PyCObject_AsVoidPtr( holder->csource );

    return true;
}

typedef struct {
    uint8_t r, g, b;
} rgb8;

typedef struct {
    int64_t time, nextTime;
    rgb8 *frameData;
    int stride;
    box2i fullDataWindow, currentDataWindow;
} FrameTarget;

typedef struct {
    PyObject_HEAD

    GdkGLConfig *glConfig;
    GtkWidget *drawingArea;
    PyObject *drawingAreaObj;
    VideoSourceHolder frameSource;
    PresentationClockHolder clock;
    GMutex *frameReadMutex, *frameRenderMutex;
    GCond *frameReadCond;
    int nextToRenderFrame;
    int readBuffer, writeBuffer, filled;
    rational frameRate;
    guint timeoutSourceID;
    PyObject *pyclock;
    box2i displayWindow;
    int firstFrame, lastFrame;
    float pixelAspectRatio;
    GLuint textureId;
    float texCoordX, texCoordY;
    bool renderOneFrame, drawOneFrame;

    FrameTarget targets[4];
    float rate;
    bool quit, textureAllocated;
    GThread *renderThread;
} py_obj_VideoWidget;

typedef struct {
    PyObject_HEAD
    PyObject *innerObj;
} py_obj_SystemPresentationClock;

static gboolean
expose( GtkWidget *widget, GdkEventExpose *event, py_obj_VideoWidget *self ) {
    GdkGLContext *glcontext = gtk_widget_get_gl_context( self->drawingArea );
    GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable( self->drawingArea );

    if( !gdk_gl_drawable_gl_begin( gldrawable, glcontext ) )
        return FALSE;

    static bool __glewInit = false;

    if( !__glewInit ) {
        glewInit();
        __glewInit = true;
    }

    // Set ourselves up with the correct aspect ratio for the space
    v2i frameSize;
    box2i_getSize( &self->displayWindow, &frameSize );

    float width = frameSize.x * self->pixelAspectRatio;
    float height = frameSize.y;

    if( width > widget->allocation.width ) {
        height *= widget->allocation.width / width;
        width = widget->allocation.width;
    }

    if( height > widget->allocation.height ) {
        width *= widget->allocation.height / height;
        height = widget->allocation.height;
    }

    // Center
    float x = widget->allocation.width * 0.5f - width * 0.5f;
    float y = widget->allocation.height * 0.5f - height * 0.5f;

    if( !self->textureAllocated ) {
        glGenTextures( 1, &self->textureId );
        glBindTexture( GL_TEXTURE_2D, self->textureId );

        if( GLEW_ARB_texture_non_power_of_two ) {
            glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, frameSize.x, frameSize.y,
                0, GL_RGB, GL_UNSIGNED_BYTE, NULL );

            self->texCoordX = 1.0f;
            self->texCoordY = 1.0f;
        }
        else {
            int texW = 1, texH = 1;

            while( texW < frameSize.x )
                texW <<= 1;

            while( texH < frameSize.y )
                texH <<= 1;

            glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, texW, texH,
                0, GL_RGB, GL_UNSIGNED_BYTE, NULL );

            self->texCoordX = (float) frameSize.x / (float) texW;
            self->texCoordY = (float) frameSize.y / (float) texH;
        }

        self->textureAllocated = true;
    }

    glLoadIdentity();
    glViewport( 0, 0, widget->allocation.width, widget->allocation.height );
    glOrtho( 0, widget->allocation.width, widget->allocation.height, 0, -1, 1 );

    glClearColor( 0.3f, 0.3f, 0.3f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );

    glBindTexture( GL_TEXTURE_2D, self->textureId );
    glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, frameSize.x, frameSize.y,
        GL_RGB, GL_UNSIGNED_BYTE, &self->targets[self->readBuffer].frameData[0] );
    checkGLError();

    glEnable( GL_TEXTURE_2D );

    glBegin( GL_QUADS );
    glTexCoord2f( 0, 0 );
    glVertex2f( x, y );
    glTexCoord2f( self->texCoordX, 0 );
    glVertex2f( x + width, y );
    glTexCoord2f( self->texCoordX, self->texCoordY );
    glVertex2f( x + width, y + height );
    glTexCoord2f( 0, self->texCoordY );
    glVertex2f( x, y + height );
    glEnd();

    glDisable( GL_TEXTURE_2D );

    if( gdk_gl_drawable_is_double_buffered( gldrawable ) )
        gdk_gl_drawable_swap_buffers( gldrawable );
    else
        glFlush();

    gdk_gl_drawable_gl_end( gldrawable );

    return TRUE;
}

int64_t
getFrameTime( rational *frameRate, int frame ) {
    return ((int64_t) frame * INT64_C(1000000000) * (int64_t)(frameRate->d)) / (int64_t)(frameRate->n) + INT64_C(1);
}

int
getTimeFrame( rational *frameRate, int64_t time ) {
    return (time * (int64_t)(frameRate->n)) / (INT64_C(1000000000) * (int64_t)(frameRate->d));
}

bool parseRational( PyObject *in, rational *out ) {
    // Accept integers as rationals
    if( PyInt_Check( in ) ) {
        out->n = PyInt_AsLong( in );
        out->d = 1;

        return true;
    }

    PyObject *numerator = PyObject_GetAttrString( in, "numerator" );

    if( numerator == NULL )
        return false;

    long n = PyInt_AsLong( numerator );
    Py_DECREF(numerator);

    if( n == -1 && PyErr_Occurred() != NULL )
        return false;

    PyObject *denominator = PyObject_GetAttrString( in, "denominator" );

    if( denominator == NULL )
        return false;

    long d = PyInt_AsLong( denominator );
    Py_DECREF(denominator);

    if( d == -1 && PyErr_Occurred() != NULL )
        return false;

    out->n = (int) n;
    out->d = (unsigned int) d;

    return true;
}

PyObject *py_getFrameTime( PyObject *self, PyObject *args ) {
    PyObject *frameRateObj;
    rational frameRate;
    int frame;

    if( !PyArg_ParseTuple( args, "Oi", &frameRateObj, &frame ) )
        return NULL;

    if( !parseRational( frameRateObj, &frameRate ) )
        return NULL;

    return Py_BuildValue( "L", getFrameTime( &frameRate, frame ) );
}

PyObject *py_getTimeFrame( PyObject *self, PyObject *args ) {
    PyObject *frameRateObj;
    rational frameRate;
    int64_t time;

    if( !PyArg_ParseTuple( args, "OL", &frameRateObj, &time ) )
        return NULL;

    if( !parseRational( frameRateObj, &frameRate ) )
        return NULL;

    return Py_BuildValue( "i", getTimeFrame( &frameRate, time ) );
}

static gboolean playSingleFrame( py_obj_VideoWidget *self );

static bool box2i_equalSize( box2i *box1, box2i *box2 ) {
    v2i size1, size2;

    box2i_getSize( box1, &size1 );
    box2i_getSize( box2, &size2 );

    return size1.x == size2.x && size1.y == size2.y;
}

static gpointer
playbackThread( py_obj_VideoWidget *self ) {
    RgbaFrame frame;

    box2i_setEmpty( &frame.fullDataWindow );

    for( ;; ) {
        int64_t startTime = self->clock.funcs->getPresentationTime( self->clock.source );

        g_mutex_lock( self->frameReadMutex );
        while( !self->quit && !self->renderOneFrame && self->filled > 2 )
            g_cond_wait( self->frameReadCond, self->frameReadMutex );

        if( self->quit )
            return NULL;

        // If restarting, reset the clock; who knows how long we've been waiting?
        if( self->filled < 0 )
            startTime = self->clock.funcs->getPresentationTime( self->clock.source );

        v2i frameSize;
        rational speed;

        box2i_getSize( &self->displayWindow, &frameSize );
        self->clock.funcs->getSpeed( self->clock.source, &speed );

        // Pull out the next frame
        int nextFrame = self->nextToRenderFrame;

        // Set up the proper write buffer
        if( !self->renderOneFrame )
            self->writeBuffer = (self->writeBuffer + 1) & 3;

        int writeBuffer = self->writeBuffer;

        FrameTarget *target = &self->targets[writeBuffer];

        // If the frame is the wrong size, reallocate it now
        if( box2i_isEmpty( &target->fullDataWindow ) ||
            !box2i_equalSize( &self->displayWindow, &target->fullDataWindow ) ) {

            if( target->frameData ) {
                free( target->frameData );
                target->frameData = NULL;
            }

            target->frameData = malloc( frameSize.y * frameSize.x * sizeof(rgba) );
            target->stride = frameSize.x;
        }

        // If our target array is the wrong size, reallocate it now
        if( box2i_isEmpty( &frame.fullDataWindow ) ||
            !box2i_equalSize( &self->displayWindow, &frame.fullDataWindow ) ) {

            frame.frameData = malloc( frameSize.y * frameSize.x * sizeof(rgba) );
            frame.stride = frameSize.x;
        }

        // The frame could shift without changing size, so we set this here just in case
        target->fullDataWindow = self->displayWindow;
        frame.fullDataWindow = self->displayWindow;

        // Clip to the set first/last frames
        if( nextFrame > self->lastFrame )
            nextFrame = self->lastFrame;
        else if( nextFrame < self->firstFrame )
            nextFrame = self->firstFrame;

        VideoFrameSourceFuncs *funcs = self->frameSource.funcs;

        g_mutex_unlock( self->frameReadMutex );

//        printf( "Start rendering %d into %d...\n", nextFrame, writeBuffer );

        // Pull the frame data from the chain
        frame.currentDataWindow = target->fullDataWindow;

        if( funcs != NULL ) {
            funcs->getFrame( self->frameSource.source, nextFrame, &frame );
        }
        else {
            // No result
            box2i_setEmpty( &frame.currentDataWindow );
        }

        target->currentDataWindow = frame.currentDataWindow;

        // Convert the results to floating-point
        for( int y = frame.currentDataWindow.min.y; y <= frame.currentDataWindow.max.y; y++ ) {
            for( int x = frame.currentDataWindow.min.x; x <= frame.currentDataWindow.max.x; x++ ) {
                rgb8 *targetData = &target->frameData
                    [(y - target->fullDataWindow.min.y) * target->stride + x - target->fullDataWindow.min.x];
                rgba *sourceData = &frame.frameData
                    [(y - frame.fullDataWindow.min.y) * frame.stride + x - frame.fullDataWindow.min.x];

                targetData->r = gamma45[sourceData->r];
                targetData->g = gamma45[sourceData->g];
                targetData->b = gamma45[sourceData->b];
            }
        }

        //usleep( 100000 );

        self->targets[writeBuffer].time = getFrameTime( &self->frameRate, nextFrame );
        int64_t endTime = self->clock.funcs->getPresentationTime( self->clock.source );

        int64_t lastDuration = endTime - startTime;

        //printf( "Rendered frame %d into %d in %f presentation seconds (at %ld)...\n", self->nextToRenderFrame, writeBuffer,
        //    ((double) endTime - (double) startTime) / 1000000000.0, endTime );
        //printf( "Presentation time %ld\n", info->_presentationTime[writeBuffer] );

        g_mutex_lock( self->frameReadMutex );
        if( self->filled < 0 ) {
            rational newSpeed;
            self->clock.funcs->getSpeed( self->clock.source, &newSpeed );

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

static gboolean
playSingleFrame( py_obj_VideoWidget *self ) {
    if( self->quit )
        return FALSE;

    if( self->filled > 0 || self->drawOneFrame ) {
        g_mutex_lock( self->frameReadMutex );
        int filled = self->filled;

        if( !self->drawOneFrame )
            self->readBuffer = (self->readBuffer + 1) & 3;

        int64_t nextPresentationTime = self->targets[self->readBuffer].nextTime;
        g_mutex_unlock( self->frameReadMutex );

        if( filled != 0 || self->drawOneFrame ) {
            gdk_window_invalidate_rect( self->drawingArea->window, &self->drawingArea->allocation, FALSE );
            //gdk_window_process_updates( self->drawingArea->window, FALSE );

            //printf( "Painted %d from %d...\n", getTimeFrame(self->frameRate, self->presentationTime[self->readBuffer]), self->readBuffer );

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
                self->clock.funcs->getSpeed( self->clock.source, &speed );

                if( speed.n != 0 ) {
                    //printf( "nextPresent: %ld, current: %ld, baseTime: %ld, seekTime: %ld\n", nextPresentationTime, self->clock->getPresentationTime(), ((SystemPresentationClock*)self->clock)->_baseTime, ((SystemPresentationClock*) self->clock)->_seekTime );

                    int timeout = ((nextPresentationTime - self->clock.funcs->getPresentationTime( self->clock.source )) * speed.d) / (speed.n * 1000000);

                    if( timeout < 0 )
                        timeout = 0;

                    self->timeoutSourceID = g_timeout_add_full(
                        G_PRIORITY_DEFAULT, timeout, (GSourceFunc) playSingleFrame, self, NULL );
                }
            }

            return FALSE;
        }
    }

    rational speed;
    self->clock.funcs->getSpeed( self->clock.source, &speed );

    if( speed.n != 0 ) {
        self->timeoutSourceID = g_timeout_add_full( G_PRIORITY_DEFAULT,
            (1000 * self->frameRate.d * speed.d) / (self->frameRate.n * abs(speed.n)),
            (GSourceFunc) playSingleFrame, self, NULL );
    }

    return FALSE;
}

static int
VideoWidget_init( py_obj_VideoWidget *self, PyObject *args, PyObject *kwds ) {
    PyObject *pyclock;
    PyObject *frameSource = NULL;

    if( !PyArg_ParseTuple( args, "O|O", &pyclock, &frameSource ) )
        return -1;

    Py_CLEAR( self->drawingAreaObj );

    if( !takePresentationClock( pyclock, &self->clock ) )
        return -1;

    if( !takeVideoSource( frameSource, &self->frameSource ) )
        return -1;

    self->glConfig = gdk_gl_config_new_by_mode ( (GdkGLConfigMode) (GDK_GL_MODE_RGB    |
                                        GDK_GL_MODE_DEPTH  |
                                        GDK_GL_MODE_DOUBLE));
    if( self->glConfig == NULL )    {
        g_print( "*** Cannot find the double-buffered visual.\n" );
        g_print( "*** Trying single-buffered visual.\n" );

        /* Try single-buffered visual */
        self->glConfig = gdk_gl_config_new_by_mode ((GdkGLConfigMode) (GDK_GL_MODE_RGB   |
                                        GDK_GL_MODE_DEPTH));
        if( self->glConfig == NULL ) {
            g_print( "*** No appropriate OpenGL-capable visual found.\n" );
            exit( 1 );
        }
    }

    box2i_set( &self->displayWindow, 0, 0, 319, 239 );

    v2i frameSize;
    box2i_getSize( &self->displayWindow, &frameSize );

    self->drawingArea = gtk_drawing_area_new();

    gtk_widget_set_gl_capability( self->drawingArea,
                                self->glConfig,
                                NULL,
                                TRUE,
                                GDK_GL_RGBA_TYPE );

    self->drawingAreaObj = pygobject_new( (GObject*) self->drawingArea );

    self->frameRate.n = 24000;
    self->frameRate.d = 1001u;

    self->frameReadMutex = g_mutex_new();
    self->frameReadCond = g_cond_new();
    self->frameRenderMutex = g_mutex_new();
    self->nextToRenderFrame = 0;
    self->filled = 3;
    self->readBuffer = 3;
    self->writeBuffer = 3;
    self->firstFrame = 0;
    self->lastFrame = INT_MAX;
    self->pixelAspectRatio = 40.0f / 33.0f;
    self->quit = false;
    self->textureId = -1;
    box2i_setEmpty( &self->targets[0].fullDataWindow );
    box2i_setEmpty( &self->targets[1].fullDataWindow );
     box2i_setEmpty( &self->targets[2].fullDataWindow );
    box2i_setEmpty( &self->targets[3].fullDataWindow );
    self->textureAllocated = false;

    g_signal_connect( G_OBJECT(self->drawingArea), "expose_event", G_CALLBACK(expose), self );

    self->renderThread = g_thread_create( (GThreadFunc) playbackThread, self, TRUE, NULL );

    return 0;
}

static void
VideoWidget_dealloc( py_obj_VideoWidget *self ) {
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

    Py_CLEAR( self->clock.source );
    Py_CLEAR( self->clock.csource );
    Py_CLEAR( self->frameSource.source );
    Py_CLEAR( self->frameSource.csource );
    Py_CLEAR( self->drawingAreaObj );

    if( self->drawingArea != NULL )
        gtk_widget_destroy( GTK_WIDGET(self->drawingArea) );

    self->ob_type->tp_free( (PyObject*) self );
}

static PyObject *
VideoWidget_stop( py_obj_VideoWidget *self ) {
    if( self->timeoutSourceID != 0 ) {
        g_source_remove( self->timeoutSourceID );
        self->timeoutSourceID = 0;
    }

    // Have the production thread play one more frame, then stop
    g_mutex_lock( self->frameReadMutex );
    self->filled = 3;

    int64_t stopTime = self->clock.funcs->getPresentationTime( self->clock.source );

    self->renderOneFrame = true;
    self->nextToRenderFrame = getTimeFrame( &self->frameRate, stopTime );
    g_cond_signal( self->frameReadCond );
    g_mutex_unlock( self->frameReadMutex );

    Py_RETURN_NONE;
}

static PyObject *
VideoWidget_play( py_obj_VideoWidget *self ) {
    if( self->timeoutSourceID != 0 ) {
        g_source_remove( self->timeoutSourceID );
        self->timeoutSourceID = 0;
    }

    // Fire up the production and playback threads from scratch
    g_mutex_lock( self->frameReadMutex );
    int64_t stopTime = self->clock.funcs->getPresentationTime( self->clock.source );
    self->nextToRenderFrame = getTimeFrame( &self->frameRate, stopTime );
    self->filled = -2;
    g_cond_signal( self->frameReadCond );
    g_mutex_unlock( self->frameReadMutex );

    playSingleFrame( self );

    Py_RETURN_NONE;
}

static PyObject *
VideoWidget_widgetObj( py_obj_VideoWidget *self ) {
    Py_INCREF( self->drawingAreaObj );
    return self->drawingAreaObj;
}

static PyObject *
VideoWidget_getDisplayWindow( py_obj_VideoWidget *self ) {
    return Py_BuildValue( "(iiii)", self->displayWindow.min.x, self->displayWindow.min.y,
        self->displayWindow.max.x, self->displayWindow.max.y );
}

static PyObject *
VideoWidget_setDisplayWindow( py_obj_VideoWidget *self, PyObject *args ) {
    box2i window;

    if( !PyArg_ParseTuple( args, "(iiii)", &window.min.x, &window.min.y, &window.max.x, &window.max.y ) )
        return NULL;

    if( box2i_isEmpty( &window ) ) {
        PyErr_SetString( PyExc_Exception, "An empty window was passed to setDisplayWindow." );
        return NULL;
    }

    g_mutex_lock( self->frameReadMutex );
    self->displayWindow = window;
    g_mutex_unlock( self->frameReadMutex );

    Py_RETURN_NONE;
}

static PyObject *
VideoWidget_getSource( py_obj_VideoWidget *self ) {
    if( self->frameSource.source == NULL )
        Py_RETURN_NONE;

    Py_INCREF(self->frameSource.source);
    return self->frameSource.source;
}

static PyObject *
VideoWidget_setSource( py_obj_VideoWidget *self, PyObject *args, void *closure ) {
    PyObject *source;

    if( !PyArg_ParseTuple( args, "O", &source ) )
        return NULL;

    g_mutex_lock( self->frameReadMutex );
    bool result = takeVideoSource( source, &self->frameSource );
    g_mutex_unlock( self->frameReadMutex );

    if( !result )
        return NULL;

    Py_RETURN_NONE;
}

static PyMethodDef VideoWidget_methods[] = {
    { "play", (PyCFunction) VideoWidget_play, METH_NOARGS,
        "Signals that the widget should start processing frames or process a speed change." },
    { "stop", (PyCFunction) VideoWidget_stop, METH_NOARGS,
        "Signals the widget to stop processing frames." },
    { "drawingArea", (PyCFunction) VideoWidget_widgetObj, METH_NOARGS,
        "Returns the drawing area used for video output." },
    { "displayWindow", (PyCFunction) VideoWidget_getDisplayWindow, METH_NOARGS,
        "Gets the display window as a tuple of min and max coordinates (minX, minY, maxX, maxY)." },
    { "source", (PyCFunction) VideoWidget_getSource, METH_NOARGS,
        "Gets the video source." },
    { "setDisplayWindow", (PyCFunction) VideoWidget_setDisplayWindow, METH_VARARGS,
        "Sets the display window using a tuple of min and max coordinates (minX, minY, maxX, maxY)." },
    { "setSource", (PyCFunction) VideoWidget_setSource, METH_VARARGS,
        "Sets the video source." },
    { NULL }
};

static PyTypeObject py_type_VideoWidget = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.VideoWidget",    // tp_name
    sizeof(py_obj_VideoWidget),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) VideoWidget_dealloc,
    .tp_init = (initproc) VideoWidget_init,
    .tp_methods = VideoWidget_methods
};

static PyMethodDef module_methods[] = {
    { "getFrameTime", (PyCFunction) py_getFrameTime, METH_VARARGS,
        "getFrameTime(rate, frame): Gets the time, in nanoseconds, of a frame at the given Rational frame rate." },
    { "getTimeFrame", (PyCFunction) py_getTimeFrame, METH_VARARGS,
        "getTimeFrame(rate, time): Gets the frame containing the given time in nanoseconds at the given Fraction frame rate." },
    { NULL }
};

void init_AVFileReader( PyObject *module );
void init_Pulldown23RemovalFilter( PyObject *module );
void init_SystemPresentationClock( PyObject *module );
void init_AlsaPlayer( PyObject *module );

PyMODINIT_FUNC
initvideo() {
    int argc = 1;
    char *arg = "dummy";
    char **argv = &arg;

    if( PyType_Ready( &py_type_VideoWidget ) < 0 )
        return;

    PyObject *m = Py_InitModule3( "video", module_methods,
        "The Fluggo Video library for Python." );

    Py_INCREF( &py_type_VideoWidget );
    PyModule_AddObject( m, "VideoWidget", (PyObject *) &py_type_VideoWidget );

    init_AVFileReader( m );
    init_Pulldown23RemovalFilter( m );
    init_SystemPresentationClock( m );
    init_AlsaPlayer( m );

    // Fill in the 0.45 gamma table
    for( int i = 0; i < 65536; i++ ) {
        gamma45[i] = (uint8_t) gamma45Func( h2f( (half) i ) );
    }

    init_pygobject();
    init_pygtk();
    gtk_gl_init( &argc, &argv );

    if( !g_thread_supported() )
        g_thread_init( NULL );
}

#if 0
gboolean
keyPressHandler( GtkWidget *widget, GdkEventKey *event, gpointer userData ) {
    VideoWidget *video = (VideoWidget*) g_object_get_data( G_OBJECT((GtkWidget*) userData), "__info" );

    Rational speed = video->getClock()->getSpeed();

    switch( event->keyval ) {
        case GDK_l:
            if( speed.n < 1 )
                speed = Rational( 1, 1 );
            else
                speed.n *= 2;
            break;

        case GDK_k:
            speed = Rational( 0, 1 );
            break;

        case GDK_j:
            if( speed.n > -1 )
                speed = Rational( -1, 1 );
            else
                speed.n *= 2;
            break;
    }

    ((SystemPresentationClock*) video->getClock())->play( speed );

    if( speed.n == 0 )
        video->stop();
    else
        video->play();

    return TRUE;
}
#endif

/*
    <source name='scene7'>
        <avsource file='Okra Principle - 7 (good take).avi' duration='5000' durationUnits='frames'>
            <stream type='video' number='0' gamma='0.45' colorspace='Rec601' />
            <stream type='audio' number='1' audioMap='stereo' />
        </avsource>
    </source>
    <clip name='shot7a/cam1/take1' source='scene7'>
        <version label='1' start='56' startUnits='frames' duration='379' durationUnits='frames'>
            <pulldown style='23' offset='3' />
        </version>
    </clip>
    <source name='scene7fostex'>
        <avsource file='scene7fostex.wav' duration='5000000' durationUnits='samples'>
            <stream type='audio' number='0' audioMap='custom'>
                <audioMap sourceChannel='left' targetChannel='center' />
            </stream>
        </avsource>
    </source>
    <clip name='shot7a/fostex/take1' source='scene7fostex'>
        <version label='1' start='5000' startUnits='ms' duration='10000' durationUnits='ms'/>
    </clip>
    <take name='scene7/shot7a/take1'>
        <version label='1'>
            <clip name='shot7a/cam1/take1' start='0' startUnits='frames' />
            <clip name='shot7a/fostex/take1' start='-56' startUnits='ms' />
        </version>
    </take>
    <timeline>
    </timeline>
*/

#if 0
int
main( int argc, char *argv[] ) {
    gtk_init( &argc, &argv );
    gtk_gl_init( &argc, &argv );

    if( !g_thread_supported() )
        g_thread_init( NULL );

/*    AVFileReader reader( "/home/james/Desktop/Demo2.avi" );
    Array2D<Rgba> array( 480, 720 );
    int i = 0;

    while( reader.ReadFrame( array ) ) {
        std::stringstream filename;
        filename << "rgba" << i++ << ".exr";

        //Header header( 720, 480, 40.0f / 33.0f );

        //RgbaOutputFile file( filename.str().c_str(), header, WRITE_RGBA );
        //file.setFrameBuffer( &array[0][0], 1, 720 );
        //file.writePixels( 480 );

        puts( filename.str().c_str() );
    }*/

    GtkWidget *window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_title( GTK_WINDOW(window), "boogidy boogidy" );

    SystemPresentationClock clock;
    clock.set( Rational( 1, 1 ), 5000LL * 1000000000LL * 1001LL / 24000LL );

    VideoWidget widget( &clock );
    GtkWidget *drawingArea = widget.getWidget();

    g_signal_connect( G_OBJECT(window), "key-press-event", G_CALLBACK(keyPressHandler), drawingArea );
    g_signal_connect( G_OBJECT(window), "delete_event", G_CALLBACK(gtk_main_quit), NULL );

    gtk_container_add( GTK_CONTAINER(window), drawingArea );
    gtk_widget_show( drawingArea );

    gtk_widget_show( window );

    gtk_main();
}
#endif


