/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2009 Brian J. Crowell <brian@fluggo.com>

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

#include <stdint.h>

#include <pygobject.h>
#include <pygtk/pygtk.h>
#include <Python.h>
#include <structmember.h>
#include "framework.h"
#include "clock.h"
#include <time.h>

#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <gdk/gdkkeysyms.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glx.h>

void *getCurrentGLContext() {
    return glXGetCurrentContext();
}

void
gl_renderToTexture( GLuint texture, int width, int height ) {
    GLuint fbo;
    glGenFramebuffersEXT( 1, &fbo );
    glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, fbo );
    glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB,
        texture, 0 );

    glLoadIdentity();
    glOrtho( 0, width, height, 0, -1, 1 );
    glViewport( 0, 0, width, height );

    glRecti( 0, 0, width, height );

    glDeleteFramebuffersEXT( 1, &fbo );
}

void
gl_printShaderErrors( GLhandleARB shader ) {
    int status;
    glGetObjectParameterivARB( shader, GL_OBJECT_COMPILE_STATUS_ARB, &status );

    if( !status ) {
        printf( "Error(s) compiling the shader:\n" );
        int infoLogLength;

        glGetObjectParameterivARB( shader, GL_OBJECT_INFO_LOG_LENGTH_ARB, &infoLogLength );

        char *infoLog = calloc( 1, infoLogLength + 1 );

        glGetInfoLogARB( shader, infoLogLength, &infoLogLength, infoLog );

        puts( infoLog );
        free( infoLog );
    }
}

void
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

bool takeAudioSource( PyObject *source, AudioSourceHolder *holder ) {
    Py_CLEAR( holder->source );
    Py_CLEAR( holder->csource );
    holder->funcs = NULL;

    if( source == NULL || source == Py_None )
        return true;

    Py_INCREF( source );
    holder->source = source;
    holder->csource = PyObject_GetAttrString( source, "_audioFrameSourceFuncs" );

    if( holder->csource == NULL ) {
        Py_CLEAR( holder->source );
        PyErr_SetString( PyExc_Exception, "The source didn't have an acceptable _audioFrameSourceFuncs attribute." );
        return false;
    }

    holder->funcs = (AudioFrameSourceFuncs*) PyCObject_AsVoidPtr( holder->csource );

    return true;
}

bool takeFrameFunc( PyObject *source, FrameFunctionHolder *holder ) {
    Py_CLEAR( holder->source );
    Py_CLEAR( holder->csource );
    holder->funcs = NULL;
    holder->constant = 0.0f;

    if( source == NULL || source == Py_None )
        return true;

    PyObject *asFloat = PyNumber_Float( source );

    if( asFloat ) {
        holder->constant = (float) PyFloat_AS_DOUBLE( asFloat );
        Py_CLEAR( asFloat );
        return true;
    }
    else {
        // Clear any errors
        PyErr_Clear();
    }

    Py_INCREF( source );
    holder->source = source;
    holder->csource = PyObject_GetAttrString( source, "_frameFunctionFuncs" );

    if( holder->csource == NULL ) {
        Py_CLEAR( holder->source );
        PyErr_SetString( PyExc_Exception, "The source didn't have an acceptable _frameFunctionFuncs attribute." );
        return false;
    }

    holder->funcs = (FrameFunctionFuncs*) PyCObject_AsVoidPtr( holder->csource );

    return true;
}

void getFrame_f16( VideoSourceHolder *source, int frameIndex, rgba_f16_frame *targetFrame ) {
    if( !source || !source->funcs ) {
        box2i_setEmpty( &targetFrame->currentDataWindow );
        return;
    }

    if( source->funcs->getFrame ) {
        source->funcs->getFrame( source->source, frameIndex, targetFrame );
        return;
    }

    if( !source->funcs->getFrame32 ) {
        box2i_setEmpty( &targetFrame->currentDataWindow );
        return;
    }

    // Allocate a new frame
    rgba_f32_frame tempFrame;
    v2i size;

    box2i_getSize( &targetFrame->fullDataWindow, &size );
    tempFrame.frameData = slice_alloc( sizeof(rgba_f32) * size.x * size.y );
    tempFrame.fullDataWindow = targetFrame->fullDataWindow;
    tempFrame.currentDataWindow = targetFrame->fullDataWindow;
    tempFrame.stride = size.x;

    source->funcs->getFrame32( source->source, frameIndex, &tempFrame );

    // Convert to f16
    int offsetX = tempFrame.currentDataWindow.min.x - tempFrame.fullDataWindow.min.x;
    int countX = tempFrame.currentDataWindow.max.x - tempFrame.currentDataWindow.min.x + 1;

    for( int y = tempFrame.currentDataWindow.min.y - tempFrame.fullDataWindow.min.y;
        y <= tempFrame.currentDataWindow.max.y - tempFrame.fullDataWindow.min.y; y++ ) {

        half_convert_from_float(
            &tempFrame.frameData[y * tempFrame.stride + offsetX].r,
            &targetFrame->frameData[y * targetFrame->stride + offsetX].r,
            countX * 4 );
    }

    targetFrame->currentDataWindow = tempFrame.currentDataWindow;

    slice_free( sizeof(rgba_f32) * size.x * size.y, tempFrame.frameData );
}

void getFrame_f32( VideoSourceHolder *source, int frameIndex, rgba_f32_frame *targetFrame ) {
    if( !source || !source->funcs ) {
        box2i_setEmpty( &targetFrame->currentDataWindow );
        return;
    }

    if( source->funcs->getFrame32 ) {
        source->funcs->getFrame32( source->source, frameIndex, targetFrame );
        return;
    }

    if( !source->funcs->getFrame ) {
        box2i_setEmpty( &targetFrame->currentDataWindow );
        return;
    }

    // Allocate a new frame
    rgba_f16_frame tempFrame;
    v2i size;

    box2i_getSize( &targetFrame->fullDataWindow, &size );
    tempFrame.frameData = slice_alloc( sizeof(rgba_f16) * size.x * size.y );
    tempFrame.fullDataWindow = targetFrame->fullDataWindow;
    tempFrame.currentDataWindow = targetFrame->fullDataWindow;
    tempFrame.stride = size.x;

    source->funcs->getFrame( source->source, frameIndex, &tempFrame );

    // Convert to f32
    int offsetX = tempFrame.currentDataWindow.min.x - tempFrame.fullDataWindow.min.x;
    int countX = tempFrame.currentDataWindow.max.x - tempFrame.currentDataWindow.min.x + 1;

    for( int y = tempFrame.currentDataWindow.min.y - tempFrame.fullDataWindow.min.y;
        y <= tempFrame.currentDataWindow.max.y - tempFrame.fullDataWindow.min.y; y++ ) {

        half_convert_to_float(
            &tempFrame.frameData[y * tempFrame.stride + offsetX].r,
            &targetFrame->frameData[y * targetFrame->stride + offsetX].r,
            countX * 4 );
    }

    targetFrame->currentDataWindow = tempFrame.currentDataWindow;

    slice_free( sizeof(rgba_f16) * size.x * size.y, tempFrame.frameData );
}

void getFrame_gl( VideoSourceHolder *source, int frameIndex, rgba_gl_frame *targetFrame ) {
    if( !source || !source->funcs ) {
        box2i_setEmpty( &targetFrame->currentDataWindow );
        return;
    }

    if( source->funcs->getFrameGL ) {
        source->funcs->getFrameGL( source->source, frameIndex, targetFrame );
        return;
    }

    // Pull 16-bit frame data from the software chain and load it
    v2i frameSize;
    box2i_getSize( &targetFrame->fullDataWindow, &frameSize );

    rgba_f16_frame frame = { NULL };
    frame.frameData = slice_alloc( sizeof(rgba_f16) * frameSize.x * frameSize.y );
    frame.fullDataWindow = targetFrame->fullDataWindow;
    frame.currentDataWindow = targetFrame->fullDataWindow;
    frame.stride = frameSize.x;

    getFrame_f16( source, frameIndex, &frame );

    // TODO: Only fill in the area specified by currentDataWindow
    glGenTextures( 1, &targetFrame->texture );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, targetFrame->texture );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA_FLOAT16_ATI, frameSize.x, frameSize.y, 0,
        GL_RGBA, GL_HALF_FLOAT_ARB, frame.frameData );

    slice_free( sizeof(rgba_f16) * frameSize.x * frameSize.y, frame.frameData );
}

int64_t
getFrameTime( const rational *frameRate, int frame ) {
    return ((int64_t) frame * INT64_C(1000000000) * (int64_t)(frameRate->d)) / (int64_t)(frameRate->n) + INT64_C(1);
}

int
getTimeFrame( const rational *frameRate, int64_t time ) {
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

PyObject *
py_getAudioData( PyObject *self, PyObject *args, PyObject *kw ) {
    PyObject *sourceObj;
    AudioSourceHolder source;
    int channels = 2, minSample, maxSample;

    static char *kwlist[] = { "source", "minSample", "maxSample", "channels", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "Oii|i", kwlist,
            &sourceObj, &minSample, &maxSample, &channels ) )
        return NULL;

    // Verify good arguments
    if( channels < 0 ) {
        PyErr_SetString( PyExc_Exception, "The number of channels was less than zero." );
        return NULL;
    }

    if( maxSample < minSample ) {
        PyErr_SetString( PyExc_Exception, "The max sample was less than the min sample." );
        return NULL;
    }

    // Prep data structures
    if( !takeAudioSource( sourceObj, &source ) )
        return NULL;

    AudioFrame frame;
    frame.frameData = PyMem_Malloc( channels * (maxSample - minSample + 1) * sizeof(float) );
    frame.channelCount = channels;
    frame.fullMinSample = minSample;
    frame.fullMaxSample = maxSample;

    if( frame.frameData == NULL ) {
        takeAudioSource( NULL, &source );
        return PyErr_NoMemory();
    }

    // Fetch the data
    source.funcs->getFrame( source.source, &frame );

    // Clear the references we took
    takeAudioSource( NULL, &source );

    // Prepare to give it to Python
    PyObject *resultChannelList = PyList_New( frame.channelCount );

    if( !resultChannelList ) {
        PyMem_Free( frame.frameData );
        return NULL;
    }

    int listLength = 0;

    if( frame.currentMaxSample >= frame.currentMinSample )
        listLength = frame.currentMaxSample - frame.currentMinSample + 1;

    for( int i = 0; i < frame.channelCount; i++ ) {
        PyObject *resultChannel = PyList_New( listLength );

        if( !resultChannel ) {
            Py_CLEAR( resultChannelList );
            PyMem_Free( frame.frameData );
            return NULL;
        }

        PyList_SET_ITEM( resultChannelList, i, resultChannel );

        for( int j = 0; j < listLength; j++ ) {
            PyObject *value = PyFloat_FromDouble( (double) frame.frameData[frame.channelCount * j + i] );

            if( !value ) {
                Py_CLEAR( resultChannelList );
                PyMem_Free( frame.frameData );
                return NULL;
            }

            PyList_SET_ITEM( resultChannel, j, value );
        }
    }

    // We can finally free the data memory
    PyMem_Free( frame.frameData );

    PyObject *result = Py_BuildValue( "iiN", frame.currentMinSample, frame.currentMaxSample, resultChannelList );

    if( !result )
        Py_CLEAR( resultChannelList );

    return result;
}

PyObject *
py_timeGetFrame( PyObject *self, PyObject *args, PyObject *kw ) {
    PyObject *dataWindowTuple = NULL, *sourceObj;
    rgba_f16_frame frame;
    box2i_set( &frame.fullDataWindow, 0, 0, 4095, 4095 );
    int minFrame, maxFrame;

    static char *kwlist[] = { "source", "minFrame", "maxFrame", "dataWindow", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "Oii|O", kwlist,
            &sourceObj, &minFrame, &maxFrame, &dataWindowTuple ) )
        return NULL;

    if( dataWindowTuple && !PyArg_ParseTuple( dataWindowTuple, "iiii",
        &frame.fullDataWindow.min.x,
        &frame.fullDataWindow.min.y,
        &frame.fullDataWindow.max.x,
        &frame.fullDataWindow.max.y ) )
        return NULL;

    v2i frameSize;
    box2i_getSize( &frame.fullDataWindow, &frameSize );

    frame.currentDataWindow = frame.fullDataWindow;
    frame.stride = frameSize.x;
    frame.frameData = PyMem_Malloc( sizeof(rgba_f16) * frameSize.x * frameSize.y );

    if( !frame.frameData )
        return PyErr_NoMemory();

    VideoSourceHolder source;

    if( !takeVideoSource( sourceObj, &source ) ) {
        PyMem_Free( frame.frameData );
        return NULL;
    }

    int64_t startTime = gettime();
    for( int i = minFrame; i <= maxFrame; i++ )
        source.funcs->getFrame( source.source, i, &frame );
    int64_t endTime = gettime();

    PyMem_Free( frame.frameData );

    if( !takeVideoSource( NULL, &source ) )
        return NULL;

    return Py_BuildValue( "L", endTime - startTime );
}

PyObject *py_writeVideo( PyObject *self, PyObject *args, PyObject *kw );

static PyMethodDef module_methods[] = {
    { "getFrameTime", (PyCFunction) py_getFrameTime, METH_VARARGS,
        "getFrameTime(rate, frame): Gets the time, in nanoseconds, of a frame at the given Rational frame rate." },
    { "getTimeFrame", (PyCFunction) py_getTimeFrame, METH_VARARGS,
        "getTimeFrame(rate, time): Gets the frame containing the given time in nanoseconds at the given Fraction frame rate." },
    { "getAudioData", (PyCFunction) py_getAudioData, METH_VARARGS | METH_KEYWORDS,
        "minSample, maxSample, data = getAudioData(source, minSample, maxSample[, channels=2]): Gets raw audio data from the source." },
    { "timeGetFrame", (PyCFunction) py_timeGetFrame, METH_VARARGS | METH_KEYWORDS,
        "timeGetFrame(source, minFrame, maxFrame, dataWindow=(0,0,1,1)): Retrieves minFrame through maxFrame from the source and returns the time it took in nanoseconds." },
    { "writeVideo", (PyCFunction) py_writeVideo, METH_VARARGS | METH_KEYWORDS,
        "TBD" },
    { NULL }
};

void init_FFVideoSource( PyObject *module );
void init_FFAudioSource( PyObject *module );
void init_Pulldown23RemovalFilter( PyObject *module );
void init_SystemPresentationClock( PyObject *module );
void init_AlsaPlayer( PyObject *module );
void init_GtkVideoWidget( PyObject *module );
void init_half( PyObject *module );
void init_VideoSequence( PyObject *module );
void init_VideoMixFilter( PyObject *module );
void init_basicframefuncs( PyObject *module );

PyMODINIT_FUNC
initmedia() {
    PyObject *m = Py_InitModule3( "media", module_methods,
        "The Fluggo Media library for Python." );

    init_half( m );
    init_FFVideoSource( m );
    init_FFAudioSource( m );
    init_Pulldown23RemovalFilter( m );
    init_SystemPresentationClock( m );
    init_AlsaPlayer( m );
    init_GtkVideoWidget( m );
    init_VideoSequence( m );
    init_VideoMixFilter( m );
    init_basicframefuncs( m );
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


