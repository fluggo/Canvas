
#include "framework.h"
#include <gtk/gtk.h>
#include <asoundlib.h>

#define F_PI 3.1415926535897932384626433832795f

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

typedef struct {
    PyObject_HEAD

    AudioSourceHolder audioSource;
    snd_pcm_t *pcmDevice;
    GThread *playbackThread;
    bool quit;
} py_obj_AlsaPlayer;

static gpointer
playbackThread( py_obj_AlsaPlayer *self ) {
    int fdCount, error;
    struct pollfd *fds;

    fdCount = snd_pcm_poll_descriptors_count( self->pcmDevice );

    if( fdCount <= 0 ) {
        printf( "Invalid poll descriptors count\n" );
        return NULL;
    }

    fds = malloc( sizeof(struct pollfd) * fdCount );

    if( fds == NULL ) {
        printf( "Out of memory in AlsaPlayer\n" );
        return NULL;
    }

    if( (error = snd_pcm_poll_descriptors( self->pcmDevice, fds, fdCount )) < 0 ) {
        printf( "Unable to obtain poll descriptors for playback: %s\n", snd_strerror( error ) );
        free( fds );
        return NULL;
    }

    const int bufferSize = 1024, channelCount = 2;
    float *data = malloc( bufferSize * channelCount * sizeof(float) );

    if( data == NULL ) {
        printf( "Out of memory in AlsaPlayer allocating output buffer\n" );
        free( fds );
        return NULL;
    }

    int min = 0;

    for( ;; ) {
        if( self->quit )
            break;

        AudioFrame frame;
        frame.channelCount = 2;
        frame.frameData = data;
        frame.fullMinSample = min;
        frame.fullMaxSample = min + bufferSize - 1;
        frame.currentMinSample = frame.fullMinSample;
        frame.currentMaxSample = frame.fullMaxSample;

        self->audioSource.funcs->getFrame( self->audioSource.source, &frame );

//        for( int i = 0; i < bufferSize; i++ ) {
//            for( int j = 0; j < channelCount; j++ ) {
//                data[i * channelCount + j] = 0.0f;
//            }
//        }

        snd_pcm_writei( self->pcmDevice, data, bufferSize );

        min += bufferSize;
    }

    snd_pcm_drop( self->pcmDevice );

    free( data );
    free( fds );

    return NULL;
}

static int
AlsaPlayer_init( py_obj_AlsaPlayer *self, PyObject *args, PyObject *kwds ) {
    PyObject *frameSource = NULL;

    if( !PyArg_ParseTuple( args, "O", &frameSource ) )
        return -1;

    if( !takeAudioSource( frameSource, &self->audioSource ) )
        return -1;

    int error;
    const char *deviceName = "default";

    if( (error = snd_pcm_open( &self->pcmDevice, "default", SND_PCM_STREAM_PLAYBACK, 0 )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Could not open PCM device %s: %s", deviceName, snd_strerror( error ) );
        return -1;
    }

    snd_pcm_hw_params_t *params = PyMem_Malloc( snd_pcm_hw_params_sizeof() );

    if( params == NULL ) {
        PyErr_NoMemory();
        return -1;
    }

    if( (error = snd_pcm_hw_params_any( self->pcmDevice, params )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Could not open configuration for playback on %s: %s", deviceName, snd_strerror( error ) );
        return -1;
    }

    if( (error = snd_pcm_hw_params_set_access( self->pcmDevice, params, SND_PCM_ACCESS_RW_INTERLEAVED )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Failed to set interleaved access on %s: %s", deviceName, snd_strerror( error ) );
        return -1;
    }

    // Set stereo channels for now
    if( (error = snd_pcm_hw_params_set_channels( self->pcmDevice, params, 2 )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Failed to set channel count to 2 on %s: %s", deviceName, snd_strerror( error ) );
        return -1;
    }

    // Grab what should be an easy-to-find format
    if( (error = snd_pcm_hw_params_set_format( self->pcmDevice, params, SND_PCM_FORMAT_FLOAT )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Failed to set sample format on %s: %s", deviceName, snd_strerror( error ) );
        return -1;
    }

    // Set a common sample rate
    if( (error = snd_pcm_hw_params_set_rate( self->pcmDevice, params, 48000, 0 )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Failed to set sample rate on %s: %s", deviceName, snd_strerror( error ) );
        return -1;
    }

    if( (error = snd_pcm_hw_params( self->pcmDevice, params )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Failed to write parameter set to %s: %s", deviceName, snd_strerror( error ) );
        return -1;
    }

    PyMem_Free( params );

    self->playbackThread = g_thread_create( (GThreadFunc) playbackThread, self, TRUE, NULL );

    return 0;
}

static void
AlsaPlayer_dealloc( py_obj_AlsaPlayer *self ) {
    self->quit = true;

    Py_CLEAR( self->audioSource.csource );
    Py_CLEAR( self->audioSource.source );

    if( self->playbackThread != NULL )
        g_thread_join( self->playbackThread );

    if( self->pcmDevice != NULL ) {
        snd_pcm_close( self->pcmDevice );
        self->pcmDevice = NULL;
    }

    self->ob_type->tp_free( (PyObject*) self );
}

static PyObject *
AlsaPlayer_getPresentationTime( py_obj_AlsaPlayer *self ) {
    snd_pcm_uframes_t avail;
    snd_htimestamp_t tstamp;
    int error;

    if( (error = snd_pcm_htimestamp( self->pcmDevice, &avail, &tstamp )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Failed to obtain current device time: %s", snd_strerror( error ) );
        return NULL;
    }

    return Py_BuildValue( "L", (int64_t) tstamp.tv_sec * INT64_C(1000000000) + (int64_t) tstamp.tv_nsec );
}

static PyMethodDef AlsaPlayer_methods[] = {
    { "getPresentationTime", (PyCFunction) AlsaPlayer_getPresentationTime, METH_NOARGS,
        "Gets the current presentation time in nanoseconds." },
    { NULL }
};

static PyTypeObject py_type_AlsaPlayer = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.AlsaPlayer",    // tp_name
    sizeof(py_obj_AlsaPlayer),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AlsaPlayer_dealloc,
    .tp_init = (initproc) AlsaPlayer_init,
    .tp_methods = AlsaPlayer_methods
};

NOEXPORT void init_AlsaPlayer( PyObject *module ) {
    if( PyType_Ready( &py_type_AlsaPlayer ) < 0 )
        return;

    Py_INCREF( &py_type_AlsaPlayer );
    PyModule_AddObject( module, "AlsaPlayer", (PyObject *) &py_type_AlsaPlayer );
}

