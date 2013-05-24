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

#include "pyframework.h"
#include <structmember.h>
#include <time.h>

typedef struct {
    video_source source;
    PyObject *csource;
} py_video_source;

EXPORT bool py_video_take_source( PyObject *obj, video_source **source_ ) {
    py_video_source *source = (py_video_source *) (*source_);

    if( source ) {
        // TODO: If we get beyond Python, just put a full ref/unref system
        // in and unref here
        Py_CLEAR( source->source.obj );
        Py_CLEAR( source->csource );
        source->source.funcs = NULL;

        g_slice_free( py_video_source, source );
        *source_ = NULL;
    }

    if( obj == NULL || obj == Py_None )
        return true;

    source = g_slice_new( py_video_source );

    Py_INCREF( obj );
    source->source.obj = obj;
    source->csource = PyObject_GetAttrString( obj, VIDEO_FRAME_SOURCE_FUNCS );

    if( !PyCapsule_IsValid( source->csource, VIDEO_FRAME_SOURCE_FUNCS ) ) {
        Py_DECREF( obj );
        g_slice_free( py_video_source, source );

        PyErr_SetString( PyExc_Exception, "The source didn't have an acceptable " VIDEO_FRAME_SOURCE_FUNCS " attribute." );
        return false;
    }

    source->source.funcs = (video_frame_source_funcs*) PyCapsule_GetPointer(
        source->csource, VIDEO_FRAME_SOURCE_FUNCS );
    *source_ = (video_source *) source;

    return true;
}

EXPORT bool py_audio_take_source( PyObject *source, AudioSourceHolder *holder ) {
    Py_CLEAR( holder->source.obj );
    Py_CLEAR( holder->csource );
    holder->source.funcs = NULL;

    if( source == NULL || source == Py_None )
        return true;

    Py_INCREF( source );
    holder->source.obj = source;
    holder->csource = PyObject_GetAttrString( source, AUDIO_FRAME_SOURCE_FUNCS );

    if( !PyCapsule_IsValid( holder->csource, AUDIO_FRAME_SOURCE_FUNCS ) ) {
        Py_CLEAR( holder->source.obj );
        PyErr_SetString( PyExc_Exception, "The source didn't have an acceptable " AUDIO_FRAME_SOURCE_FUNCS " attribute." );
        return false;
    }

    holder->source.funcs = (AudioFrameSourceFuncs*) PyCapsule_GetPointer(
        holder->csource, AUDIO_FRAME_SOURCE_FUNCS );

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

    return Py_BuildValue( "L", get_frame_time( &frameRate, frame ) );
}

PyObject *py_getTimeFrame( PyObject *self, PyObject *args ) {
    PyObject *frameRateObj;
    rational frameRate;
    int64_t time;

    if( !PyArg_ParseTuple( args, "OL", &frameRateObj, &time ) )
        return NULL;

    if( !py_parse_rational( frameRateObj, &frameRate ) )
        return NULL;

    return Py_BuildValue( "i", get_time_frame( &frameRate, time ) );
}

PyObject *
py_timeGetFrame( PyObject *self, PyObject *args, PyObject *kw ) {
    PyObject *dataWindowTuple = NULL, *sourceObj;
    rgba_frame_f16 frame;
    box2i_set( &frame.full_window, 0, 0, 4095, 4095 );
    int minFrame, maxFrame;

    static char *kwlist[] = { "source", "min_frame", "max_frame", "data_window", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "Oii|O", kwlist,
            &sourceObj, &minFrame, &maxFrame, &dataWindowTuple ) )
        return NULL;

    if( dataWindowTuple && !PyArg_ParseTuple( dataWindowTuple, "iiii",
        &frame.full_window.min.x,
        &frame.full_window.min.y,
        &frame.full_window.max.x,
        &frame.full_window.max.y ) )
        return NULL;

    v2i frameSize;
    box2i_get_size( &frame.full_window, &frameSize );

    frame.current_window = frame.full_window;
    frame.data = PyMem_Malloc( sizeof(rgba_f16) * frameSize.x * frameSize.y );

    if( !frame.data )
        return PyErr_NoMemory();

    video_source *source = NULL;

    if( !py_video_take_source( sourceObj, &source ) ) {
        PyMem_Free( frame.data );
        return NULL;
    }

    int64_t startTime = gettime();
    for( int i = minFrame; i <= maxFrame; i++ )
        video_get_frame_f16( source, i, &frame );
    int64_t endTime = gettime();

    PyMem_Free( frame.data );

    if( !py_video_take_source( NULL, &source ) )
        return NULL;

    return Py_BuildValue( "L", endTime - startTime );
}

static bool __enable_logging = false;

PyObject *
py_enable_glib_logging( PyObject *self, PyObject *args ) {
    PyObject *bool_obj = NULL;

    if( !PyArg_ParseTuple( args, "|O", &bool_obj ) )
        return NULL;

    if( bool_obj == NULL ) {
        __enable_logging = true;
    }
    else {
        int value = PyObject_IsTrue( bool_obj );

        if( value == -1 )
            return NULL;

        __enable_logging = value ? true : false;
    }

    Py_RETURN_NONE;
}

#define GL_CONTEXT_NAME     "fluggo.media.process.Offscreen GL Context"

static void
py_destroy_offscreen_gl_context( PyObject *obj ) {
    void *context = PyCapsule_GetPointer( obj, GL_CONTEXT_NAME );

    if( context )
        gl_destroy_offscreen_context( context );
}

static PyObject *
py_create_offscreen_gl_context( PyObject *self, PyObject *args ) {
    void *context = gl_create_offscreen_context();

    if( !context )
        return NULL;

    return PyCapsule_New( context, GL_CONTEXT_NAME, py_destroy_offscreen_gl_context );
}

static PyObject *
py_set_current_gl_context( PyObject *self, PyObject *capsule ) {
    if( capsule == Py_None ) {
        gl_set_current_context( NULL );
    }
    else {
        // BJC: Note that we don't hang on to the PyObject here; as it stands now,
        // it's up to the caller to hang on to the capsule object until it unsets
        // the current context. I don't know if that's the best idea, but keeping
        // track of it also involves keeping track of it on multiple threads.
        void *context = PyCapsule_GetPointer( capsule, GL_CONTEXT_NAME );

        if( !context )
            return NULL;

        gl_set_current_context( context );
    }

    Py_RETURN_NONE;
}

static PyObject *
py_check_context_supported( PyObject *self, PyObject *args ) {
    gl_ensure_context();

    // Check for required extensions
    bool hard_mode_available =
        GLEW_VERSION_2_1 &&
        GLEW_ATI_texture_float &&
        GLEW_ARB_texture_rectangle &&
        GLEW_EXT_framebuffer_object &&
        GLEW_ARB_half_float_pixel;

    if( hard_mode_available )
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}

static PyMethodDef module_methods[] = {
    { "get_frame_time", (PyCFunction) py_getFrameTime, METH_VARARGS,
        "get_frame_time(rate, frame): Gets the time, in nanoseconds, of a frame at the given Rational frame rate." },
    { "get_time_frame", (PyCFunction) py_getTimeFrame, METH_VARARGS,
        "get_time_frame(rate, time): Gets the frame containing the given time in nanoseconds at the given Fraction frame rate." },
    { "time_get_frame", (PyCFunction) py_timeGetFrame, METH_VARARGS | METH_KEYWORDS,
        "timeGetFrame(source, min_frame, max_frame, data_window=(0,0,1,1)): Retrieves min_frame through max_frame from the source and returns the time it took in nanoseconds." },
    { "enable_glib_logging", (PyCFunction) py_enable_glib_logging, METH_VARARGS,
        "enable_glib_logging([True]): Connects GLib log reporting to the Python logging module." },
    { "create_offscreen_gl_context", (PyCFunction) py_create_offscreen_gl_context, METH_NOARGS,
        "create_offscreen_gl_context(): Returns an opaque object that can be used with set_current_gl_context." },
    { "set_current_gl_context", (PyCFunction) py_set_current_gl_context, METH_O,
        "set_current_gl_context( context ): Sets the current GL context to one returned from create_offscreen_gl_context (or None)." },
    { "check_context_supported", (PyCFunction) py_check_context_supported, METH_NOARGS,
        "check_context_supported(): Returns true if the available GL context supports video rendering (a context is created if it hasn't already been done). If false, video rendering will be unavailable." },
    { NULL }
};

static PyObject *__getLoggerFunc;

static void python_logger( const gchar *log_domain,
                           GLogLevelFlags log_level,
                           const gchar *message,
                           gpointer user_data ) {
    // Skip out early if we don't want to report this
    if( !__enable_logging )
        return;

    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject *logger = PyObject_CallFunction( __getLoggerFunc, "s", log_domain );

    if( logger == NULL ) {
        g_printerr( "fluggo.media.process: Exception when calling logger.getLogger\n" );
        g_log_default_handler( log_domain, log_level, message, user_data );
        PyErr_Clear();
        PyGILState_Release( gstate );
        return;
    }

    // BJC: I'm not sure the GLib interpretations of these levels line up with
    // the Python logging interpretations of the same
    PyObject *result;

    if( log_level & G_LOG_LEVEL_ERROR ) {
        result = PyObject_CallMethod( logger, "error", "s", message );
    }
    else if( log_level & G_LOG_LEVEL_CRITICAL ) {
        result = PyObject_CallMethod( logger, "critical", "s", message );
    }
    else if( log_level & G_LOG_LEVEL_WARNING ) {
        result = PyObject_CallMethod( logger, "warning", "s", message );
    }
    else if( (log_level & G_LOG_LEVEL_MESSAGE) || (log_level & G_LOG_LEVEL_INFO) ) {
        result = PyObject_CallMethod( logger, "info", "s", message );
    }
    else if( log_level & G_LOG_LEVEL_DEBUG ) {
        result = PyObject_CallMethod( logger, "debug", "s", message );
    }
    else {
        Py_CLEAR(logger);
        PyGILState_Release( gstate );
        return;
    }

    Py_CLEAR(logger);

    if( result ) {
        Py_CLEAR(result);
    }
    else {
        g_printerr( "fluggo.media.process: Exception when calling logger routine\n" );
        g_log_default_handler( log_domain, log_level, message, user_data );
        PyErr_Clear();
    }

    PyGILState_Release( gstate );
}

bool init_basetypes( PyObject *module );
void init_AudioSource( PyObject *module );
void init_VideoSource( PyObject *module );
void init_CodecPacketSource( PyObject *module );
void init_CodedImageSource( PyObject *module );
void init_DVReconstructionFilter( PyObject *module );
void init_DVSubsampleFilter( PyObject *module );
void init_Pulldown23RemovalFilter( PyObject *module );
void init_SystemPresentationClock( PyObject *module );
void init_AudioPassThroughFilter( PyObject *module );
void init_VideoSequence( PyObject *module );
void init_VideoMixFilter( PyObject *module );
void init_VideoPassThroughFilter( PyObject *module );
void init_SolidColorVideoSource( PyObject *module );
void init_EmptyVideoSource( PyObject *module );
void init_basicframefuncs( PyObject *module );
void init_AudioWorkspace( PyObject *module );
void init_VideoWorkspace( PyObject *module );
void init_AudioFrame( PyObject *module );
void init_RgbaFrameF16( PyObject *module );
void init_RgbaFrameF32( PyObject *module );
void init_VideoScaler( PyObject *module );
void init_VideoPullQueue( PyObject *module );
void init_AnimationFunc( PyObject *module );
void init_FrameFuncPassThroughFilter( PyObject *module );
void init_VideoGainOffsetFilter( PyObject *module );
void init_MPEG2SubsampleFilter( PyObject *module );

EXPORT PyMODINIT_FUNC
PyInit_process() {
    __getLoggerFunc = NULL;

    static PyModuleDef mdef = {
        .m_base = PyModuleDef_HEAD_INIT,
        .m_name = "process",
        .m_doc = "The Fluggo media processing library for Python.",

        // TODO: Consider making use of this; see Python docs
        .m_size = -1,
        .m_methods = module_methods,

        // TODO: Consider supporting module cleanup
    };

    PyObject *m = PyModule_Create( &mdef );

    init_half();
    if( !init_basetypes( m ) )
        return NULL;
    init_AudioSource( m );
    init_VideoSource( m );
    init_CodecPacketSource( m );
    init_CodedImageSource( m );
    init_DVReconstructionFilter( m );
    init_DVSubsampleFilter( m );
    init_Pulldown23RemovalFilter( m );
    init_SystemPresentationClock( m );
    init_VideoSequence( m );
    init_VideoMixFilter( m );
    init_AudioPassThroughFilter( m );
    init_VideoPassThroughFilter( m );
    init_SolidColorVideoSource( m );
    init_EmptyVideoSource( m );
    init_basicframefuncs( m );
    init_AudioWorkspace( m );
    init_VideoWorkspace( m );
    init_AudioFrame( m );
    init_RgbaFrameF16( m );
    init_RgbaFrameF32( m );
    init_VideoScaler( m );
    init_VideoPullQueue( m );
    init_AnimationFunc( m );
    init_FrameFuncPassThroughFilter( m );
    init_VideoGainOffsetFilter( m );
    init_MPEG2SubsampleFilter( m );

    PyModule_AddObject( m, "NS_PER_SEC", PyLong_FromLongLong( NS_PER_SEC ) );

    // Connect GLib logging support to Python logging
    PyObject *logging_module = PyImport_ImportModule( "logging" );
    __getLoggerFunc = PyObject_GetAttrString( logging_module, "getLogger" );
    Py_CLEAR(logging_module);

    g_log_set_default_handler( python_logger, NULL );

    return m;
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


