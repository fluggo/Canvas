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

#include "pyframework.h"
#include <structmember.h>
#include "clock.h"
#include <time.h>

EXPORT bool py_video_takeSource( PyObject *source, VideoSourceHolder *holder ) {
    Py_CLEAR( holder->source.obj );
    Py_CLEAR( holder->csource );
    holder->source.funcs = NULL;

    if( source == NULL || source == Py_None )
        return true;

    Py_INCREF( source );
    holder->source.obj = source;
    holder->csource = PyObject_GetAttrString( source, VIDEO_FRAME_SOURCE_FUNCS );

    if( holder->csource == NULL ) {
        Py_CLEAR( holder->source.obj );
        PyErr_SetString( PyExc_Exception, "The source didn't have an acceptable " VIDEO_FRAME_SOURCE_FUNCS " attribute." );
        return false;
    }

    holder->source.funcs = (VideoFrameSourceFuncs*) PyCObject_AsVoidPtr( holder->csource );

    return true;
}

EXPORT bool py_audio_takeSource( PyObject *source, AudioSourceHolder *holder ) {
    Py_CLEAR( holder->source.obj );
    Py_CLEAR( holder->csource );
    holder->source.funcs = NULL;

    if( source == NULL || source == Py_None )
        return true;

    Py_INCREF( source );
    holder->source.obj = source;
    holder->csource = PyObject_GetAttrString( source, AUDIO_FRAME_SOURCE_FUNCS );

    if( holder->csource == NULL ) {
        Py_CLEAR( holder->source.obj );
        PyErr_SetString( PyExc_Exception, "The source didn't have an acceptable " AUDIO_FRAME_SOURCE_FUNCS " attribute." );
        return false;
    }

    holder->source.funcs = (AudioFrameSourceFuncs*) PyCObject_AsVoidPtr( holder->csource );

    return true;
}

PyObject *py_getFrameTime( PyObject *self, PyObject *args ) {
    PyObject *frameRateObj;
    rational frameRate;
    int frame;

    if( !PyArg_ParseTuple( args, "Oi", &frameRateObj, &frame ) )
        return NULL;

    if( !py_parse_rational( frameRateObj, &frameRate ) )
        return NULL;

    return Py_BuildValue( "L", getFrameTime( &frameRate, frame ) );
}

PyObject *py_getTimeFrame( PyObject *self, PyObject *args ) {
    PyObject *frameRateObj;
    rational frameRate;
    int64_t time;

    if( !PyArg_ParseTuple( args, "OL", &frameRateObj, &time ) )
        return NULL;

    if( !py_parse_rational( frameRateObj, &frameRate ) )
        return NULL;

    return Py_BuildValue( "i", getTimeFrame( &frameRate, time ) );
}

PyObject *
py_getAudioData( PyObject *self, PyObject *args, PyObject *kw ) {
    PyObject *sourceObj;
    AudioSourceHolder source;
    int channels = 2, minSample, maxSample;

    static char *kwlist[] = { "source", "min_sample", "max_sample", "channels", NULL };

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
    if( !py_audio_takeSource( sourceObj, &source ) )
        return NULL;

    AudioFrame frame;
    frame.frameData = PyMem_Malloc( channels * (maxSample - minSample + 1) * sizeof(float) );
    frame.channelCount = channels;
    frame.fullMinSample = minSample;
    frame.fullMaxSample = maxSample;

    if( frame.frameData == NULL ) {
        py_audio_takeSource( NULL, &source );
        return PyErr_NoMemory();
    }

    // Fetch the data
    source.source.funcs->getFrame( source.source.obj, &frame );

    // Clear the references we took
    py_audio_takeSource( NULL, &source );

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
    rgba_frame_f16 frame;
    box2i_set( &frame.fullDataWindow, 0, 0, 4095, 4095 );
    int minFrame, maxFrame;

    static char *kwlist[] = { "source", "min_frame", "max_frame", "data_window", NULL };

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

    VideoSourceHolder source = { .csource = NULL };

    if( !py_video_takeSource( sourceObj, &source ) ) {
        PyMem_Free( frame.frameData );
        return NULL;
    }

    int64_t startTime = gettime();
    for( int i = minFrame; i <= maxFrame; i++ )
        source.source.funcs->getFrame( source.source.obj, i, &frame );
    int64_t endTime = gettime();

    PyMem_Free( frame.frameData );

    if( !py_video_takeSource( NULL, &source ) )
        return NULL;

    return Py_BuildValue( "L", endTime - startTime );
}

PyObject *py_frame_func_get( PyObject *self, PyObject *args, PyObject *kw );

static PyMethodDef module_methods[] = {
    { "get_frame_time", (PyCFunction) py_getFrameTime, METH_VARARGS,
        "get_frame_time(rate, frame): Gets the time, in nanoseconds, of a frame at the given Rational frame rate." },
    { "get_time_frame", (PyCFunction) py_getTimeFrame, METH_VARARGS,
        "get_time_frame(rate, time): Gets the frame containing the given time in nanoseconds at the given Fraction frame rate." },
    { "get_audio_data", (PyCFunction) py_getAudioData, METH_VARARGS | METH_KEYWORDS,
        "min_sample, max_sample, data = getAudioData(source, min_sample, max_sample[, channels=2]): Gets raw audio data from the source." },
    { "time_get_frame", (PyCFunction) py_timeGetFrame, METH_VARARGS | METH_KEYWORDS,
        "timeGetFrame(source, min_frame, max_frame, data_window=(0,0,1,1)): Retrieves min_frame through max_frame from the source and returns the time it took in nanoseconds." },
    { "frame_func_get", (PyCFunction) py_frame_func_get, METH_VARARGS | METH_KEYWORDS,
        "Get a list of values from a frame function.\n"
        "\n"
        "value_list = frame_func_get(source, frames[, div=1])\n"
        "\n"
        "source: A frame function.\n"
        "frames: An integer or a list of integers of the frames to get values for.\n"
        "div: Optional integer to divide each frame by, to support subsampling the function." },
    { NULL }
};

void init_basetypes( PyObject *module );
void init_VideoSource( PyObject *module );
void init_CodecPacketSource( PyObject *module );
void init_CodedImageSource( PyObject *module );
void init_DVReconstructionFilter( PyObject *module );
void init_DVSubsampleFilter( PyObject *module );
void init_Pulldown23RemovalFilter( PyObject *module );
void init_SystemPresentationClock( PyObject *module );
void init_AlsaPlayer( PyObject *module );
void init_AudioPassThroughFilter( PyObject *module );
void init_VideoSequence( PyObject *module );
void init_VideoMixFilter( PyObject *module );
void init_VideoPassThroughFilter( PyObject *module );
void init_SolidColorVideoSource( PyObject *module );
void init_EmptyVideoSource( PyObject *module );
void init_basicframefuncs( PyObject *module );
void init_Workspace( PyObject *module );
void init_RgbaFrameF16( PyObject *module );
void init_RgbaFrameF32( PyObject *module );
void init_VideoScaler( PyObject *module );
void init_VideoPullQueue( PyObject *module );

EXPORT PyMODINIT_FUNC
initprocess() {
    PyObject *m = Py_InitModule3( "process", module_methods,
        "The Fluggo media processing library for Python." );

    init_half();
    init_basetypes( m );
    init_VideoSource( m );
    init_CodecPacketSource( m );
    init_CodedImageSource( m );
    init_DVReconstructionFilter( m );
    init_DVSubsampleFilter( m );
    init_Pulldown23RemovalFilter( m );
    init_SystemPresentationClock( m );
    init_AlsaPlayer( m );
    init_VideoSequence( m );
    init_VideoMixFilter( m );
    init_AudioPassThroughFilter( m );
    init_VideoPassThroughFilter( m );
    init_SolidColorVideoSource( m );
    init_EmptyVideoSource( m );
    init_basicframefuncs( m );
    init_Workspace( m );
    init_RgbaFrameF16( m );
    init_RgbaFrameF32( m );
    init_VideoScaler( m );
    init_VideoPullQueue( m );

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


