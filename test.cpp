
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
    int32_t n;
    uint32_t d;
} py_obj_Rational;

static PyTypeObject py_type_Rational = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.Rational",    // tp_name
    sizeof(py_obj_Rational)    // tp_basicsize
};

typedef struct {
    PyObject_HEAD

    GdkGLConfig *glConfig;
    GtkWidget *drawingArea;
    PyObject *drawingAreaObj;
    PyObject *frameSource;
    int timer;
    GMutex *frameReadMutex;
    GCond *frameReadCond;
    int lastDisplayedFrame, nextToRenderFrame;
    int readBuffer, writeBuffer, filled;
    py_obj_Rational *frameRate;
    guint timeoutSourceID;
    PyObject *pyclock;
    IPresentationClock *clock;
    int firstFrame, lastFrame;
    float pixelAspectRatio;
    GLuint textureId;
    float texCoordX, texCoordY;
    int frameWidth, frameHeight;

    int64_t presentationTime[4];
    int64_t nextPresentationTime[4];
    Array2D<uint8_t[3]> targets[4];
    float rate;
    bool quit, textureAllocated;
    GThread *renderThread;
} py_obj_VideoWidget;

static PyTypeObject py_type_VideoWidget = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.VideoWidget",    // tp_name
    sizeof(py_obj_VideoWidget)    // tp_basicsize
};

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
    float width = self->frameWidth * self->pixelAspectRatio;
    float height = self->frameHeight;

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
            glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, self->frameWidth, self->frameHeight,
                0, GL_RGB, GL_UNSIGNED_BYTE, NULL );

            self->texCoordX = 1.0f;
            self->texCoordY = 1.0f;
        }
        else {
            int texW = 1, texH = 1;

            while( texW < self->frameWidth )
                texW <<= 1;

            while( texH < self->frameHeight )
                texH <<= 1;

            glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, texW, texH,
                0, GL_RGB, GL_UNSIGNED_BYTE, NULL );

            self->texCoordX = (float) self->frameWidth / (float) texW;
            self->texCoordY = (float) self->frameHeight / (float) texH;
        }

        self->textureAllocated = true;
    }

    glLoadIdentity();
    glViewport( 0, 0, widget->allocation.width, widget->allocation.height );
    glOrtho( 0, widget->allocation.width, widget->allocation.height, 0, -1, 1 );

    glClearColor( 0.3f, 0.3f, 0.3f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );

    glBindTexture( GL_TEXTURE_2D, self->textureId );
    glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, self->frameWidth, self->frameHeight,
        GL_RGB, GL_UNSIGNED_BYTE, &self->targets[self->readBuffer][0][0] );
    checkGLError();

    glEnable( GL_TEXTURE_2D );

    glBegin( GL_QUADS );
    glTexCoord2f( 0, self->texCoordY );
    glVertex2f( x, y );
    glTexCoord2f( self->texCoordX, self->texCoordY );
    glVertex2f( x + width, y );
    glTexCoord2f( self->texCoordX, 0 );
    glVertex2f( x + width, y + height );
    glTexCoord2f( 0, 0 );
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
getFrameTime( py_obj_Rational *frameRate, int frame ) {
    return (int64_t) frame * INT64_C(1000000000) * (int64_t)(frameRate->d) / (int64_t)(frameRate->n);
}

gpointer
playbackThread( py_obj_VideoWidget *self ) {
    AVFileReader reader( "/home/james/Videos/Okra - 79b,100.avi" );
    Pulldown23RemovalFilter filter( &reader, 0, false );
    Array2D<Rgba> array( self->frameHeight, self->frameWidth );

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
        for( int y = 0; y < self->frameHeight; y++ ) {
            for( int x = 0; x < self->frameWidth; x++ ) {
                self->targets[writeBuffer][self->frameHeight - y - 1][x][0] = (uint8_t) __gamma45( array[y][x].r );
                self->targets[writeBuffer][self->frameHeight - y - 1][x][1] = (uint8_t) __gamma45( array[y][x].g );
                self->targets[writeBuffer][self->frameHeight - y - 1][x][2] = (uint8_t) __gamma45( array[y][x].b );
            }
        }

        //usleep( 100000 );

        self->presentationTime[writeBuffer] = getFrameTime( self->frameRate, nextFrame );
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
            while( getFrameTime( self->frameRate, ++self->nextToRenderFrame ) < endTime + lastDuration );
        }
        else if( speed.n < 0 ) {
            while( getFrameTime( self->frameRate, --self->nextToRenderFrame ) > endTime - lastDuration );
        }

        self->nextPresentationTime[writeBuffer] = getFrameTime( self->frameRate, self->nextToRenderFrame );
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
        (1000 * self->frameRate->d * speed.d) / (self->frameRate->n * abs(speed.n)),
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
    Py_CLEAR( self->frameRate );
    Py_CLEAR( self->drawingAreaObj );

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

    self->frameWidth = 720;
    self->frameHeight = 480;

    self->drawingArea = gtk_drawing_area_new();
    gtk_widget_set_size_request( self->drawingArea, self->frameWidth, self->frameHeight );

    gtk_widget_set_gl_capability( self->drawingArea,
                                self->glConfig,
                                NULL,
                                TRUE,
                                GDK_GL_RGBA_TYPE );

    self->drawingAreaObj = pygobject_new( (GObject*) self->drawingArea );

    PyObject *tuple = Py_BuildValue( "iI", 24000, 1001u );
    self->frameRate = (py_obj_Rational*) PyObject_CallObject( (PyObject*) &py_type_Rational, tuple );
    Py_CLEAR( tuple );

    self->frameReadMutex = g_mutex_new();
    self->frameReadCond = g_cond_new();
    self->nextToRenderFrame = 5000;
    self->filled = -1;
    self->readBuffer = 3;
    self->writeBuffer = 3;
    self->firstFrame = 0;
    self->lastFrame = 6000;
    self->pixelAspectRatio = 40.0f / 33.0f;
    self->quit = false;
    self->textureId = -1;
    self->targets[0].resizeErase( self->frameHeight, self->frameWidth );
    self->targets[1].resizeErase( self->frameHeight, self->frameWidth );
    self->targets[2].resizeErase( self->frameHeight, self->frameWidth );
    self->targets[3].resizeErase( self->frameHeight, self->frameWidth );
    self->textureAllocated = false;

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
    Py_CLEAR( self->drawingAreaObj );
    Py_CLEAR( self->frameRate );

    gtk_widget_destroy( GTK_WIDGET(self->drawingArea) );
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

static PyObject *
VideoWidget_widgetObj( py_obj_VideoWidget *self ) {
    Py_INCREF( self->drawingAreaObj );
    return self->drawingAreaObj;
}

static PyMethodDef VideoWidget_methods[] = {
    { "play", (PyCFunction) VideoWidget_play, METH_NOARGS,
        "Signals that the widget should start processing frames or process a speed change." },
    { "stop", (PyCFunction) VideoWidget_stop, METH_NOARGS,
        "Signals the widget to stop processing frames." },
    { "drawingArea", (PyCFunction) VideoWidget_widgetObj, METH_NOARGS,
        "Returns the drawing area used for video output." },
    { NULL }
};

/***************** SystemPresentationClock *********/
static PyTypeObject py_type_SystemPresentationClock = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.SystemPresentationClock",    // tp_name
    sizeof(py_obj_SystemPresentationClock)    // tp_basicsize
};

void
clock_destr( void *data ) {
    delete (SystemPresentationClock*)(IPresentationClock*) data;
}

static int
SystemPresentationClock_init( py_obj_SystemPresentationClock *self, PyObject *args, PyObject *kwds ) {
    Py_CLEAR( self->innerObj );
    self->innerObj = PyCObject_FromVoidPtr( (IPresentationClock*) new SystemPresentationClock(), clock_destr );

    return 0;
}

static void
SystemPresentationClock_dealloc( py_obj_SystemPresentationClock *self ) {
    Py_CLEAR( self->innerObj );
    self->ob_type->tp_free( (PyObject*) self );
}

static PyObject *
SystemPresentationClock_play( py_obj_SystemPresentationClock *self, PyObject *args ) {
    int n, d;

    if( !PyArg_ParseTuple( args, "(ii)", &n, &d ) )
        return NULL;

    if( d == 0 ) {
        PyErr_SetString( PyExc_Exception, "Can't have a denominator is zero." );
        return NULL;
    }

    if( d < 0 ) {
        n *= -1;
        d *= -1;
    }

    SystemPresentationClock *clock = (SystemPresentationClock*)(IPresentationClock*) PyCObject_AsVoidPtr( self->innerObj );
    clock->play( Rational( n, (unsigned int) d ) );

    Py_RETURN_NONE;
}

static PyObject *
SystemPresentationClock_set( py_obj_SystemPresentationClock *self, PyObject *args ) {
    int n, d;
    int64_t time;

    if( !PyArg_ParseTuple( args, "(ii)L", &n, &d, &time ) )
        return NULL;

    if( d == 0 ) {
        PyErr_SetString( PyExc_Exception, "Can't have a denominator is zero." );
        return NULL;
    }

    if( d < 0 ) {
        n *= -1;
        d *= -1;
    }

    SystemPresentationClock *clock = (SystemPresentationClock*)(IPresentationClock*) PyCObject_AsVoidPtr( self->innerObj );
    clock->set( Rational( n, (unsigned int) d ), time );

    Py_RETURN_NONE;
}

static PyMethodDef SystemPresentationClock_methods[] = {
    { "play", (PyCFunction) SystemPresentationClock_play, METH_VARARGS,
        "Starts the clock at the current spot." },
    { "set", (PyCFunction) SystemPresentationClock_set, METH_VARARGS,
        "Sets the speed and current time." },
    { NULL }
};

static PyMemberDef SystemPresentationClock_members[] = {
    { "_obj", T_OBJECT, offsetof(py_obj_SystemPresentationClock, innerObj),
        READONLY, "Internal clock interface." },
    { NULL }
};

/************* Rational ********/
static int
Rational_init( py_obj_Rational *self, PyObject *args, PyObject *kwds ) {
    if( !PyArg_ParseTuple( args, "iI", &self->n, &self->d ) )
        return -1;

    return 0;
}

static PyMemberDef Rational_members[] = {
    { "n", T_LONG, offsetof(py_obj_Rational, n),
        0, "Numerator." },
    { "d", T_ULONG, offsetof(py_obj_Rational, d),
        0, "Denominator." },
    { NULL }
};

static PyMethodDef module_methods[] = {
    { NULL }
};

PyMODINIT_FUNC
initvideo() {
    int argc = 1;
    char *arg = "dummy";
    char **argv = &arg;

    py_type_Rational.tp_flags = Py_TPFLAGS_DEFAULT;
    py_type_Rational.tp_new = PyType_GenericNew;
    py_type_Rational.tp_init = (initproc) Rational_init;
    py_type_Rational.tp_members = Rational_members;

    if( PyType_Ready( &py_type_Rational ) < 0 )
        return;

    py_type_VideoWidget.tp_flags = Py_TPFLAGS_DEFAULT;
    py_type_VideoWidget.tp_new = PyType_GenericNew;
    py_type_VideoWidget.tp_dealloc = (destructor) VideoWidget_dealloc;
    py_type_VideoWidget.tp_init = (initproc) VideoWidget_init;
    py_type_VideoWidget.tp_methods = VideoWidget_methods;

    if( PyType_Ready( &py_type_VideoWidget ) < 0 )
        return;

    py_type_SystemPresentationClock.tp_flags = Py_TPFLAGS_DEFAULT;
    py_type_SystemPresentationClock.tp_new = PyType_GenericNew;
    py_type_SystemPresentationClock.tp_dealloc = (destructor) SystemPresentationClock_dealloc;
    py_type_SystemPresentationClock.tp_init = (initproc) SystemPresentationClock_init;
    py_type_SystemPresentationClock.tp_methods = SystemPresentationClock_methods;
    py_type_SystemPresentationClock.tp_members = SystemPresentationClock_members;

    if( PyType_Ready( &py_type_SystemPresentationClock ) < 0 )
        return;

    PyObject *m = Py_InitModule3( "video", module_methods,
        "The Fluggo Video library for Python." );

    Py_INCREF( &py_type_SystemPresentationClock );
    PyModule_AddObject( m, "SystemPresentationClock", (PyObject *) &py_type_SystemPresentationClock );

    Py_INCREF( &py_type_VideoWidget );
    PyModule_AddObject( m, "VideoWidget", (PyObject *) &py_type_VideoWidget );

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


