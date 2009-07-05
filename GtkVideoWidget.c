
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
    GMutex *frameReadMutex;
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
} py_obj_GtkVideoWidget;

static void _gl_initialize( py_obj_GtkVideoWidget *self ) {
    static bool __glewInit = false;

    if( !__glewInit ) {
        glewInit();
        __glewInit = true;
    }

    v2i frameSize;
    box2i_getSize( &self->displayWindow, &frameSize );

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
}

void _gl_softLoadTexture( py_obj_GtkVideoWidget *self ) {
    // Load texture
    v2i frameSize;
    box2i_getSize( &self->displayWindow, &frameSize );

    glBindTexture( GL_TEXTURE_2D, self->textureId );
    glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, frameSize.x, frameSize.y,
        GL_RGB, GL_UNSIGNED_BYTE, &self->targets[self->readBuffer].frameData[0] );
    checkGLError();
}

void _gl_draw( py_obj_GtkVideoWidget *self ) {
    // Set ourselves up with the correct aspect ratio for the space
    v2i frameSize;
    box2i_getSize( &self->displayWindow, &frameSize );

    v2i widgetSize = {
        .x = self->drawingArea->allocation.width,
        .y = self->drawingArea->allocation.height };

    float width = frameSize.x * self->pixelAspectRatio;
    float height = frameSize.y;

    if( width > widgetSize.x ) {
        height *= widgetSize.x / width;
        width = widgetSize.x;
    }

    if( height > widgetSize.y ) {
        width *= widgetSize.y / height;
        height = widgetSize.y;
    }

    // Center
    float x = widgetSize.x * 0.5f - width * 0.5f;
    float y = widgetSize.y * 0.5f - height * 0.5f;

    // Set up for drawing
    glLoadIdentity();
    glViewport( 0, 0, widgetSize.x, widgetSize.y );
    glOrtho( 0, widgetSize.x, widgetSize.y, 0, -1, 1 );

    glClearColor( 0.3f, 0.3f, 0.3f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );

    // Render texture onto quad
    glBindTexture( GL_TEXTURE_2D, self->textureId );
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
}

static gboolean
expose( GtkWidget *widget, GdkEventExpose *event, py_obj_GtkVideoWidget *self ) {
    GdkGLContext *glcontext = gtk_widget_get_gl_context( self->drawingArea );
    GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable( self->drawingArea );

    if( !gdk_gl_drawable_gl_begin( gldrawable, glcontext ) )
        return FALSE;

    // Render here
    _gl_initialize( self );
    _gl_softLoadTexture( self );
    _gl_draw( self );

    // Flush buffers
    if( gdk_gl_drawable_is_double_buffered( gldrawable ) )
        gdk_gl_drawable_swap_buffers( gldrawable );
    else
        glFlush();

    gdk_gl_drawable_gl_end( gldrawable );

    return TRUE;
}

static gboolean playSingleFrame( py_obj_GtkVideoWidget *self );

static bool box2i_equalSize( box2i *box1, box2i *box2 ) {
    v2i size1, size2;

    box2i_getSize( box1, &size1 );
    box2i_getSize( box2, &size2 );

    return size1.x == size2.x && size1.y == size2.y;
}

static gpointer
playbackThread( py_obj_GtkVideoWidget *self ) {
    RgbaFrame frame;

    box2i_setEmpty( &frame.fullDataWindow );

    for( ;; ) {
        int64_t startTime = self->clock.funcs->getPresentationTime( self->clock.source );

        g_mutex_lock( self->frameReadMutex );
        while( !self->quit && !self->renderOneFrame && self->filled > 2 )
            g_cond_wait( self->frameReadCond, self->frameReadMutex );

        if( self->quit ) {
            g_mutex_unlock( self->frameReadMutex );
            return NULL;
        }

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
playSingleFrame( py_obj_GtkVideoWidget *self ) {
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
GtkVideoWidget_init( py_obj_GtkVideoWidget *self, PyObject *args, PyObject *kwds ) {
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
GtkVideoWidget_dealloc( py_obj_GtkVideoWidget *self ) {
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

    if( self->frameReadMutex != NULL ) {
        g_mutex_free( self->frameReadMutex );
        self->frameReadMutex = NULL;
    }

    if( self->frameReadCond != NULL ) {
        g_cond_free( self->frameReadCond );
        self->frameReadCond = NULL;
    }

    self->ob_type->tp_free( (PyObject*) self );
}

static PyObject *
GtkVideoWidget_stop( py_obj_GtkVideoWidget *self ) {
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
GtkVideoWidget_play( py_obj_GtkVideoWidget *self ) {
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
GtkVideoWidget_widgetObj( py_obj_GtkVideoWidget *self ) {
    Py_INCREF( self->drawingAreaObj );
    return self->drawingAreaObj;
}

static PyObject *
GtkVideoWidget_getDisplayWindow( py_obj_GtkVideoWidget *self ) {
    return Py_BuildValue( "(iiii)", self->displayWindow.min.x, self->displayWindow.min.y,
        self->displayWindow.max.x, self->displayWindow.max.y );
}

static PyObject *
GtkVideoWidget_setDisplayWindow( py_obj_GtkVideoWidget *self, PyObject *args ) {
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
GtkVideoWidget_getSource( py_obj_GtkVideoWidget *self ) {
    if( self->frameSource.source == NULL )
        Py_RETURN_NONE;

    Py_INCREF(self->frameSource.source);
    return self->frameSource.source;
}

static PyObject *
GtkVideoWidget_setSource( py_obj_GtkVideoWidget *self, PyObject *args, void *closure ) {
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

static PyMethodDef GtkVideoWidget_methods[] = {
    { "play", (PyCFunction) GtkVideoWidget_play, METH_NOARGS,
        "Signals that the widget should start processing frames or process a speed change." },
    { "stop", (PyCFunction) GtkVideoWidget_stop, METH_NOARGS,
        "Signals the widget to stop processing frames." },
    { "drawingArea", (PyCFunction) GtkVideoWidget_widgetObj, METH_NOARGS,
        "Returns the drawing area used for video output." },
    { "displayWindow", (PyCFunction) GtkVideoWidget_getDisplayWindow, METH_NOARGS,
        "Gets the display window as a tuple of min and max coordinates (minX, minY, maxX, maxY)." },
    { "source", (PyCFunction) GtkVideoWidget_getSource, METH_NOARGS,
        "Gets the video source." },
    { "setDisplayWindow", (PyCFunction) GtkVideoWidget_setDisplayWindow, METH_VARARGS,
        "Sets the display window using a tuple of min and max coordinates (minX, minY, maxX, maxY)." },
    { "setSource", (PyCFunction) GtkVideoWidget_setSource, METH_VARARGS,
        "Sets the video source." },
    { NULL }
};

static PyTypeObject py_type_GtkVideoWidget = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.GtkVideoWidget",    // tp_name
    sizeof(py_obj_GtkVideoWidget),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) GtkVideoWidget_dealloc,
    .tp_init = (initproc) GtkVideoWidget_init,
    .tp_methods = GtkVideoWidget_methods
};

static inline float clampf( float x, float min, float max ) {
    return (x < min) ? min : ((x > max) ? max : x);
}

static inline float gamma45Func( float input ) {
    return clampf( powf( input, 0.45f ) * 255.0f, 0.0f, 255.0f );
}

NOEXPORT void init_GtkVideoWidget( PyObject *m ) {
    int argc = 1;
    char *arg = "dummy";
    char **argv = &arg;

    if( PyType_Ready( &py_type_GtkVideoWidget ) < 0 )
        return;

    Py_INCREF( &py_type_GtkVideoWidget );
    PyModule_AddObject( m, "GtkVideoWidget", (PyObject *) &py_type_GtkVideoWidget );

    // Fill in the 0.45 gamma table
    half *h = malloc( sizeof(half) * 65536 );
    float *f = malloc( sizeof(float) * 65536 );

    for( int i = 0; i < 65536; i++ )
        h[i] = (half) i;

    half_convert_to_float( h, f, 65536 );
    free( h );

    for( int i = 0; i < 65536; i++ )
        gamma45[i] = (uint8_t) gamma45Func( f[i] );

    free( f );

    init_pygobject();
    init_pygtk();
    gtk_gl_init( &argc, &argv );

    if( !g_thread_supported() )
        g_thread_init( NULL );
}

