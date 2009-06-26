
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

static PyMethodDef module_methods[] = {
    { "getFrameTime", (PyCFunction) py_getFrameTime, METH_VARARGS,
        "getFrameTime(rate, frame): Gets the time, in nanoseconds, of a frame at the given Rational frame rate." },
    { "getTimeFrame", (PyCFunction) py_getTimeFrame, METH_VARARGS,
        "getTimeFrame(rate, time): Gets the frame containing the given time in nanoseconds at the given Fraction frame rate." },
    { "getAudioData", (PyCFunction) py_getAudioData, METH_VARARGS | METH_KEYWORDS,
        "minSample, maxSample, data = getAudioData(source, minSample, maxSample[, channels=2]): Gets raw audio data from the source." },
    { NULL }
};

void init_AVFileReader( PyObject *module );
void init_FFAudioReader( PyObject *module );
void init_Pulldown23RemovalFilter( PyObject *module );
void init_SystemPresentationClock( PyObject *module );
void init_AlsaPlayer( PyObject *module );
void init_GtkVideoWidget( PyObject *module );

PyMODINIT_FUNC
initvideo() {
    PyObject *m = Py_InitModule3( "video", module_methods,
        "The Fluggo Video library for Python." );

    init_AVFileReader( m );
    init_FFAudioReader( m );
    init_Pulldown23RemovalFilter( m );
    init_SystemPresentationClock( m );
    init_AlsaPlayer( m );
    init_GtkVideoWidget( m );
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


