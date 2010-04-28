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

#include <Python.h>
#include <structmember.h>
#include "framework.h"
#include "clock.h"
#include <time.h>

#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glx.h>

EXPORT void
__gl_checkError(const char *file, const unsigned long line) {
    int error = glGetError();

    switch( error ) {
        case GL_NO_ERROR:
            return;

        case GL_INVALID_OPERATION:
            g_warning( "%s:%lu: Invalid operation", file, line );
            return;

        case GL_INVALID_VALUE:
            g_warning( "%s:%lu: Invalid value", file, line );
            return;

        case GL_INVALID_ENUM:
            g_warning( "%s:%lu: Invalid enum", file, line );
            return;

        default:
            g_warning( "%s:%lu: Other GL error", file, line );
            return;
    }
}

void *getCurrentGLContext() {
    return glXGetCurrentContext();
}

void
gl_renderToTexture( rgba_gl_frame *frame ) {
    v2i frameSize;
    box2i_getSize( &frame->fullDataWindow, &frameSize );

    GLuint fbo;
    glGenFramebuffersEXT( 1, &fbo );
    glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, fbo );
    glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB,
        frame->texture, 0 );

    glLoadIdentity();
    glOrtho( 0, frameSize.x, 0, frameSize.y, -1, 1 );
    glViewport( 0, 0, frameSize.x, frameSize.y );

    glBegin( GL_QUADS );
    glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
    glTexCoord2i( 0, 0 );
    glVertex2i( 0, 0 );
    glTexCoord2i( frameSize.x, 0 );
    glVertex2i( frameSize.x, 0 );
    glTexCoord2i( frameSize.x, frameSize.y );
    glVertex2i( frameSize.x, frameSize.y );
    glTexCoord2i( 0, frameSize.y );
    glVertex2i( 0, frameSize.y );
    glEnd();

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

        char *infoLog = g_malloc0( infoLogLength + 1 );

        glGetInfoLogARB( shader, infoLogLength, &infoLogLength, infoLog );

        puts( infoLog );
        g_free( infoLog );
    }
}

EXPORT void
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

EXPORT bool takeVideoSource( PyObject *source, VideoSourceHolder *holder ) {
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

EXPORT bool takeAudioSource( PyObject *source, AudioSourceHolder *holder ) {
    Py_CLEAR( holder->source );
    Py_CLEAR( holder->csource );
    holder->funcs = NULL;

    if( source == NULL || source == Py_None )
        return true;

    Py_INCREF( source );
    holder->source = source;
    holder->csource = PyObject_GetAttrString( source, AUDIO_FRAME_SOURCE_FUNCS );

    if( holder->csource == NULL ) {
        Py_CLEAR( holder->source );
        PyErr_SetString( PyExc_Exception, "The source didn't have an acceptable " AUDIO_FRAME_SOURCE_FUNCS " attribute." );
        return false;
    }

    holder->funcs = (AudioFrameSourceFuncs*) PyCObject_AsVoidPtr( holder->csource );

    return true;
}

EXPORT bool takeFrameFunc( PyObject *source, FrameFunctionHolder *holder ) {
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
    holder->csource = PyObject_GetAttrString( source, FRAME_FUNCTION_FUNCS );

    if( holder->csource == NULL ) {
        Py_CLEAR( holder->source );
        PyErr_SetString( PyExc_Exception, "The source didn't have an acceptable " FRAME_FUNCTION_FUNCS " attribute." );
        return false;
    }

    holder->funcs = (FrameFunctionFuncs*) PyCObject_AsVoidPtr( holder->csource );

    return true;
}

EXPORT void getFrame_f16( video_source *source, int frameIndex, rgba_f16_frame *targetFrame ) {
    if( !source || !source->funcs ) {
        box2i_setEmpty( &targetFrame->currentDataWindow );
        return;
    }

    if( source->funcs->getFrame ) {
        source->funcs->getFrame( source->obj, frameIndex, targetFrame );
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
    tempFrame.frameData = g_slice_alloc( sizeof(rgba_f32) * size.x * size.y );
    tempFrame.fullDataWindow = targetFrame->fullDataWindow;
    tempFrame.currentDataWindow = targetFrame->fullDataWindow;
    tempFrame.stride = size.x;

    source->funcs->getFrame32( source->obj, frameIndex, &tempFrame );

    // Convert to f16
    int offsetX = tempFrame.currentDataWindow.min.x - tempFrame.fullDataWindow.min.x;
    int countX = tempFrame.currentDataWindow.max.x - tempFrame.currentDataWindow.min.x + 1;

    if( countX > 0 ) {
        for( int y = tempFrame.currentDataWindow.min.y - tempFrame.fullDataWindow.min.y;
            y <= tempFrame.currentDataWindow.max.y - tempFrame.fullDataWindow.min.y; y++ ) {

            half_convert_from_float(
                &tempFrame.frameData[y * tempFrame.stride + offsetX].r,
                &targetFrame->frameData[y * targetFrame->stride + offsetX].r,
                countX * 4 );
        }
    }

    targetFrame->currentDataWindow = tempFrame.currentDataWindow;

    g_slice_free1( sizeof(rgba_f32) * size.x * size.y, tempFrame.frameData );
}

EXPORT void getFrame_f32( video_source *source, int frameIndex, rgba_f32_frame *targetFrame ) {
    if( !source || !source->funcs ) {
        box2i_setEmpty( &targetFrame->currentDataWindow );
        return;
    }

    if( source->funcs->getFrame32 ) {
        source->funcs->getFrame32( source->obj, frameIndex, targetFrame );
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
    tempFrame.frameData = g_slice_alloc( sizeof(rgba_f16) * size.x * size.y );
    tempFrame.fullDataWindow = targetFrame->fullDataWindow;
    tempFrame.currentDataWindow = targetFrame->fullDataWindow;
    tempFrame.stride = size.x;

    source->funcs->getFrame( source->obj, frameIndex, &tempFrame );

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

    g_slice_free1( sizeof(rgba_f16) * size.x * size.y, tempFrame.frameData );
}

EXPORT void getFrame_gl( video_source *source, int frameIndex, rgba_gl_frame *targetFrame ) {
    if( !source || !source->funcs ) {
        box2i_setEmpty( &targetFrame->currentDataWindow );
        return;
    }

    if( source->funcs->getFrameGL ) {
        source->funcs->getFrameGL( source->obj, frameIndex, targetFrame );
        return;
    }

    // Pull 16-bit frame data from the software chain and load it
    v2i frameSize;
    box2i_getSize( &targetFrame->fullDataWindow, &frameSize );

    rgba_f16_frame frame = { NULL };
    frame.frameData = g_slice_alloc0( sizeof(rgba_f16) * frameSize.x * frameSize.y );
    frame.fullDataWindow = targetFrame->fullDataWindow;
    frame.currentDataWindow = targetFrame->fullDataWindow;
    frame.stride = frameSize.x;

    getFrame_f16( source, frameIndex, &frame );

    // TODO: Only fill in the area specified by currentDataWindow
    glGenTextures( 1, &targetFrame->texture );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, targetFrame->texture );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA_FLOAT16_ATI, frameSize.x, frameSize.y, 0,
        GL_RGBA, GL_HALF_FLOAT_ARB, frame.frameData );

    g_slice_free1( sizeof(rgba_f16) * frameSize.x * frameSize.y, frame.frameData );
}

EXPORT int64_t
getFrameTime( const rational *frameRate, int frame ) {
    return ((int64_t) frame * INT64_C(1000000000) * (int64_t)(frameRate->d)) / (int64_t)(frameRate->n) + INT64_C(1);
}

EXPORT int
getTimeFrame( const rational *frameRate, int64_t time ) {
    return (time * (int64_t)(frameRate->n)) / (INT64_C(1000000000) * (int64_t)(frameRate->d));
}

static PyObject *fraction;

EXPORT bool parseRational( PyObject *in, rational *out ) {
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

EXPORT PyObject *makeFraction( rational *in ) {
    PyObject *args = Py_BuildValue( "iI", in->n, in->d );
    PyObject *result = PyObject_CallObject( fraction, args );

    Py_CLEAR( args );
    return result;
}

EXPORT PyObject *
py_make_box2f( box2f *box ) {
    // TODO: Probably make a named tuple out of this
    return Py_BuildValue( "ffff", box->min.x, box->min.y, box->max.x, box->max.y );
}

EXPORT PyObject *
py_make_box2i( box2i *box ) {
    // TODO: Probably make a named tuple out of this
    return Py_BuildValue( "iiii", box->min.x, box->min.y, box->max.x, box->max.y );
}

EXPORT PyObject *
py_make_v2f( v2f *v ) {
    // TODO: Probably make a named tuple out of this
    return Py_BuildValue( "ff", v->x, v->y );
}

EXPORT PyObject *
py_make_v2i( v2i *v ) {
    // TODO: Probably make a named tuple out of this
    return Py_BuildValue( "ii", v->x, v->y );
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

    if( !takeVideoSource( sourceObj, &source ) ) {
        PyMem_Free( frame.frameData );
        return NULL;
    }

    int64_t startTime = gettime();
    for( int i = minFrame; i <= maxFrame; i++ )
        source.source.funcs->getFrame( source.source.obj, i, &frame );
    int64_t endTime = gettime();

    PyMem_Free( frame.frameData );

    if( !takeVideoSource( NULL, &source ) )
        return NULL;

    return Py_BuildValue( "L", endTime - startTime );
}

PyObject *py_writeVideo( PyObject *self, PyObject *args, PyObject *kw );
PyObject *py_get_frame_f32( PyObject *self, PyObject *args, PyObject *kw );

static PyMethodDef module_methods[] = {
    { "get_frame_time", (PyCFunction) py_getFrameTime, METH_VARARGS,
        "get_frame_time(rate, frame): Gets the time, in nanoseconds, of a frame at the given Rational frame rate." },
    { "get_time_frame", (PyCFunction) py_getTimeFrame, METH_VARARGS,
        "get_time_frame(rate, time): Gets the frame containing the given time in nanoseconds at the given Fraction frame rate." },
    { "get_audio_data", (PyCFunction) py_getAudioData, METH_VARARGS | METH_KEYWORDS,
        "min_sample, max_sample, data = getAudioData(source, min_sample, max_sample[, channels=2]): Gets raw audio data from the source." },
    { "time_get_frame", (PyCFunction) py_timeGetFrame, METH_VARARGS | METH_KEYWORDS,
        "timeGetFrame(source, min_frame, max_frame, data_window=(0,0,1,1)): Retrieves min_frame through max_frame from the source and returns the time it took in nanoseconds." },
    { "write_video", (PyCFunction) py_writeVideo, METH_VARARGS | METH_KEYWORDS,
        "TBD" },
    { "get_frame_f32", (PyCFunction) py_get_frame_f32, METH_VARARGS | METH_KEYWORDS,
        "Get a frame of video from a video source.\n"
        "\n"
        "frame = get_frame_f32(source, frame, data_window)" },
    { NULL }
};

void init_FFVideoSource( PyObject *module );
void init_FFAudioSource( PyObject *module );
void init_FFContainer( PyObject *module );
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
void init_RgbaFrameF32( PyObject *module );

EXPORT PyMODINIT_FUNC
initprocess() {
    PyObject *m = Py_InitModule3( "process", module_methods,
        "The Fluggo media processing library for Python." );

    PyObject *fractions = PyImport_ImportModule( "fractions" );

    if( fractions == NULL )
        Py_FatalError( "Could not find the fractions module." );

    fraction = PyObject_GetAttrString( fractions, "Fraction" );
    Py_CLEAR( fractions );

    init_half();
    init_FFVideoSource( m );
    init_FFAudioSource( m );
    init_FFContainer( m );
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
    init_RgbaFrameF32( m );
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


