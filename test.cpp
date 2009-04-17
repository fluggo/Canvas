
#include <Python.h>
#include "framework.h"
#include "clock.h"

#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <gdk/gdkkeysyms.h>
#include <GL/glew.h>
#include <GL/gl.h>

#include <sstream>
#include <string>
#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <sys/timerfd.h>

using namespace Iex;
using namespace Imf;


//static float gamma22Func( float input ) {
//    return powf( input, 2.2 );
//}

// (16 / 255) ^ 2.2
const float __gamma22Base = 0.002262953f;

// (235 / 255) ^ 2.2 - __gamma22Base
const float __gamma22Extent = 0.835527791f - __gamma22Base;
const float __gamma22Fixer = 1.0f / __gamma22Extent;
const float __gammaCutoff = 21.0f / 255.0f;

inline float clamppowf( float x, float y ) {
    if( x < 0.0f )
        return 0.0f;

    if( x > 1.0f )
        return 1.0f;

    return powf( x, y );
}

inline float clampf( float x ) {
    return (x < 0.0f) ? 0.0f : ((x > 1.0f) ? 1.0f : x);
}

static float gamma45Func( float input ) {
    return Imath::clamp( powf( input, 0.45f ) * 255.0f, 0.0f, 255.0f );
}

static halfFunction<half> __gamma45( gamma45Func, half( -256.0f ), half( 256.0f ) );

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
    PyObject_HEAD

    GdkGLConfig *glConfig;
    GtkWidget *drawingArea;
    PyObject *frameSource;
    int timer;
    GMutex *frameReadMutex;
    GCond *frameReadCond;
    int lastDisplayedFrame, nextToRenderFrame;
    int readBuffer, writeBuffer, filled;
    Rational frameRate;
    guint timeoutSourceID;
    PyObject *pyclock;
    IPresentationClock *clock;
    int firstFrame, lastFrame;
    float pixelAspectRatio;

    int64_t presentationTime[4];
    int64_t nextPresentationTime[4];
    Array2D<uint8_t[3]> targets[4];
    float rate;
    bool quit;
    GThread *renderThread;
} py_obj_VideoWidget;

static PyTypeObject py_type_VideoWidget = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.VideoWidget",    // tp_name
    sizeof(py_obj_VideoWidget)    // tp_basicsize
};

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

    int width = (int)(720 * self->pixelAspectRatio);
    int height = 480;

    glLoadIdentity();
    glViewport( 0, 0, width, height );
    glOrtho( 0, width, height, 0, -1, 1 );

    glClearColor( 0.0f, 1.0f, 0.0f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );

    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, 720, 480,
        0, GL_RGB, GL_UNSIGNED_BYTE, &self->targets[self->readBuffer][0][0] );
    checkGLError();

    glEnable( GL_TEXTURE_2D );

    glBegin( GL_QUADS );
    glTexCoord2f( 0, 1 );
    glVertex2i( 0, 0 );
    glTexCoord2f( 1, 1 );
    glVertex2i( width, 0 );
    glTexCoord2f( 1, 0 );
    glVertex2i( width, height );
    glTexCoord2f( 0, 0 );
    glVertex2i( 0, height );
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
getFrameTime( Rational *frameRate, int frame ) {
    return (int64_t) frame * INT64_C(1000000000) * (int64_t)(frameRate->d) / (int64_t)(frameRate->n);
}

gpointer
playbackThread( py_obj_VideoWidget *self ) {
    AVFileReader reader( "/home/james/Videos/Okra - 79b,100.avi" );
    Pulldown23RemovalFilter filter( &reader, 0, false );
    Array2D<Rgba> array( 480, 720 );

    for( ;; ) {
        int64_t startTime = self->clock->getPresentationTime();
        g_mutex_lock( self->frameReadMutex );
        Rational speed = self->clock->getSpeed();

        while( !self->quit && self->filled > 2 )
            g_cond_wait( self->frameReadCond, self->frameReadMutex );

        if( self->quit )
            return NULL;

        if( self->filled < 0 )
            startTime = self->clock->getPresentationTime();

        int nextFrame = self->nextToRenderFrame;
        int writeBuffer = (self->writeBuffer = (self->writeBuffer + 1) & 3);
        g_mutex_unlock( self->frameReadMutex );

//        printf( "Start rendering %d into %d...\n", nextFrame, writeBuffer );

        if( nextFrame > self->lastFrame )
            nextFrame = self->lastFrame;
        else if( nextFrame < self->firstFrame )
            nextFrame = self->firstFrame;

        filter.GetFrame( nextFrame, array );

        // Convert the results to floating-point
        for( int y = 0; y < 480; y++ ) {
            for( int x = 0; x < 720; x++ ) {
                self->targets[writeBuffer][479 - y][x][0] = (uint8_t) __gamma45( array[y][x].r );
                self->targets[writeBuffer][479 - y][x][1] = (uint8_t) __gamma45( array[y][x].g );
                self->targets[writeBuffer][479 - y][x][2] = (uint8_t) __gamma45( array[y][x].b );
            }
        }

        //usleep( 100000 );

        self->presentationTime[writeBuffer] = getFrameTime( &self->frameRate, nextFrame );
        int64_t endTime = self->clock->getPresentationTime();

        int64_t lastDuration = endTime - startTime;

        //printf( "Rendered frame %d into %d in %f presentation seconds...\n", _nextToRenderFrame, buffer,
        //    ((double) endTime - (double) startTime) / 1000000000.0 );
        //printf( "Presentation time %ld\n", info->_presentationTime[writeBuffer] );

        g_mutex_lock( self->frameReadMutex );
        if( self->filled < 0 ) {
            Rational newSpeed = self->clock->getSpeed();

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

            // Write where the reader will read next
            self->writeBuffer = self->readBuffer;
        }

        self->filled++;

        if( lastDuration < INT64_C(0) )
            lastDuration *= INT64_C(-1);

        if( speed.n > 0 ) {
            while( getFrameTime( &self->frameRate, ++self->nextToRenderFrame ) < endTime + lastDuration );
        }
        else if( speed.n < 0 ) {
            while( getFrameTime( &self->frameRate, --self->nextToRenderFrame ) > endTime - lastDuration );
        }

        self->nextPresentationTime[writeBuffer] = getFrameTime( &self->frameRate, self->nextToRenderFrame );
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
    if( self->filled > 0 ) {
        g_mutex_lock( self->frameReadMutex );
        int filled = self->filled;
        self->readBuffer = (self->readBuffer + 1) & 3;
        int64_t nextPresentationTime = self->nextPresentationTime[self->readBuffer];
        g_mutex_unlock( self->frameReadMutex );

        if( filled != 0 ) {
            gdk_window_invalidate_rect( self->drawingArea->window, &self->drawingArea->allocation, FALSE );
            gdk_window_process_updates( self->drawingArea->window, FALSE );

            //printf( "Painted %ld from %d...\n", info->_presentationTime[info->readBuffer], info->readBuffer );

            g_mutex_lock( self->frameReadMutex );

            self->filled--;

            g_cond_signal( self->frameReadCond );
            g_mutex_unlock( self->frameReadMutex );

            Rational speed = self->clock->getSpeed();

            int timeout = (nextPresentationTime - self->clock->getPresentationTime()) * speed.d / (speed.n * 1000000);
            //printf( "timeout %d\n", timeout );

            if( timeout < 0 )
                timeout = 0;

            self->timeoutSourceID = g_timeout_add( timeout, (GSourceFunc) playSingleFrame, self );
            return FALSE;
        }
    }

    Rational speed = self->clock->getSpeed();

    self->timeoutSourceID = g_timeout_add(
        (1000 * self->frameRate.d * speed.d) / (self->frameRate.n * abs(speed.n)),
        (GSourceFunc) playSingleFrame, self );
    return FALSE;
}

static int
VideoWidget_init( py_obj_VideoWidget *self, PyObject *args, PyObject *kwds ) {
    PyObject *pyclock;

    if( !PyArg_ParseTuple( args, "O", &pyclock ) )
        return -1;

    PyObject *pycclock;

    if( (pycclock = PyObject_GetAttrString( pyclock, "_obj" )) == NULL )
        return -1;

    if( !PyCObject_Check( pycclock ) ) {
        PyErr_SetString( PyExc_Exception, "Given clock object doesn't have a _obj attribute with the clock implementation." );
        return -1;
    }

    Py_CLEAR( self->pyclock );
    Py_INCREF( pycclock );

    self->pyclock = pycclock;
    self->clock = (IPresentationClock*) PyCObject_AsVoidPtr( pycclock );

    Py_CLEAR( self->frameSource );

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

    self->drawingArea = gtk_drawing_area_new();
    gtk_widget_set_size_request( self->drawingArea, 720, 480 );

    gtk_widget_set_gl_capability( self->drawingArea,
                                self->glConfig,
                                NULL,
                                TRUE,
                                GDK_GL_RGBA_TYPE );

    self->frameReadMutex = g_mutex_new();
    self->frameReadCond = g_cond_new();
    self->nextToRenderFrame = 5000;
    self->frameRate = Rational( 24000, 1001 );
    self->filled = -1;
    self->readBuffer = 3;
    self->writeBuffer = 3;
    self->firstFrame = 0;
    self->lastFrame = 6000;
    self->pixelAspectRatio = 40.0f / 33.0f;
    self->quit = false;
    self->targets[0].resizeErase( 480, 720 );
    self->targets[1].resizeErase( 480, 720 );
    self->targets[2].resizeErase( 480, 720 );
    self->targets[3].resizeErase( 480, 720 );

    g_signal_connect( G_OBJECT(self->drawingArea), "expose_event", G_CALLBACK(expose), self );

    g_timeout_add( 0, (GSourceFunc) playSingleFrame, self );
    self->renderThread = g_thread_create( (GThreadFunc) playbackThread, self, TRUE, NULL );

    return 0;
}

static void
VideoWidget_dealloc( py_obj_VideoWidget *self ) {
    // Stop the render thread
    g_mutex_lock( self->frameReadMutex );
    self->quit = true;
    g_cond_signal( self->frameReadCond );
    g_mutex_unlock( self->frameReadMutex );

    g_thread_join( self->renderThread );

    Py_CLEAR( self->pyclock );
    Py_CLEAR( self->frameSource );

    self->ob_type->tp_free( (PyObject*) self );
}

static PyObject *
VideoWidget_stop( py_obj_VideoWidget *self ) {
    if( self->timeoutSourceID != 0 ) {
        g_source_remove( self->timeoutSourceID );
        self->timeoutSourceID = 0;
    }

    // Just stop the production thread
    g_mutex_lock( self->frameReadMutex );
    self->filled = 3;
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
    self->filled = -2;
    g_cond_signal( self->frameReadCond );
    g_mutex_unlock( self->frameReadMutex );

    playSingleFrame( self );

    Py_RETURN_NONE;
}

static PyMethodDef module_methods[] = {
    { NULL }
};

static PyMethodDef VideoWidget_methods[] = {
    { "play", (PyCFunction) VideoWidget_play, METH_NOARGS,
        "Signals that the widget should start processing frames or process a speed change." },
    { "stop", (PyCFunction) VideoWidget_stop, METH_NOARGS,
        "Signals the widget to stop processing frames." },
    { NULL }
};

PyMODINIT_FUNC
initvideo() {
    py_type_VideoWidget.tp_flags = Py_TPFLAGS_DEFAULT;
    py_type_VideoWidget.tp_new = PyType_GenericNew;
    py_type_VideoWidget.tp_dealloc = (destructor) VideoWidget_dealloc;
    py_type_VideoWidget.tp_init = (initproc) VideoWidget_init;
    py_type_VideoWidget.tp_methods = VideoWidget_methods;

    if( PyType_Ready( &py_type_VideoWidget ) < 0 )
        return;

    PyObject *m = Py_InitModule3( "video", module_methods,
        "The Fluggo Video library for Python." );

    Py_INCREF( &py_type_VideoWidget );
    PyModule_AddObject( m, "VideoWidget", (PyObject *) &py_type_VideoWidget );
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


