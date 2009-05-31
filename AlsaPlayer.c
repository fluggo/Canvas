
#include "framework.h"
#include <gtk/gtk.h>
#include <asoundlib.h>
#include "clock.h"

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

    int nextSample;
    int64_t seekTime, baseTime;
    AudioSourceHolder audioSource;
    snd_pcm_t *pcmDevice;
    GThread *playbackThread;
    GMutex *mutex;
    GCond *cond;
    bool quit, stop;
    rational rate;
} py_obj_AlsaPlayer;

static gpointer
playbackThread( py_obj_AlsaPlayer *self ) {
    int fdCount, error;
    struct pollfd *fds;
    snd_pcm_status_t *status;

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

    snd_pcm_status_malloc( &status );

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

    for( ;; ) {
        g_mutex_lock( self->mutex );

        if( self->stop )
            snd_pcm_drop( self->pcmDevice );

        // Set the base time for time calculations
        snd_pcm_status( self->pcmDevice, status );
        snd_htimestamp_t tstamp;

        snd_pcm_status_get_trigger_htstamp( status, &tstamp );

        self->baseTime = (int64_t) tstamp.tv_sec * INT64_C(1000000000) + (int64_t) tstamp.tv_nsec;

        while( !self->quit && self->stop )
            g_cond_wait( self->cond, self->mutex );

        if( snd_pcm_state( self->pcmDevice ) == SND_PCM_STATE_SETUP )
            snd_pcm_prepare( self->pcmDevice );

        //printf( "%s\n", snd_pcm_state_name( snd_pcm_state( self->pcmDevice ) ) );

        if( self->quit ) {
            g_mutex_unlock( self->mutex );
            break;
        }

        int nextSample = self->nextSample;
        self->nextSample += bufferSize;

        g_mutex_unlock( self->mutex );

        AudioFrame frame;
        frame.channelCount = 2;
        frame.frameData = data;
        frame.fullMinSample = nextSample;
        frame.fullMaxSample = nextSample + bufferSize - 1;
        frame.currentMinSample = frame.fullMinSample;
        frame.currentMaxSample = frame.fullMaxSample;

        self->audioSource.funcs->getFrame( self->audioSource.source, &frame );
        snd_pcm_writei( self->pcmDevice, data, bufferSize );
    }

    snd_pcm_drop( self->pcmDevice );
    snd_pcm_status_free( status );

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

    //printf( "%s\n", snd_pcm_state_name( snd_pcm_state( self->pcmDevice ) ) );

    self->mutex = g_mutex_new();
    self->cond = g_cond_new();
    self->stop = true;
    self->rate.n = 48000;
    self->rate.d = 1;

    self->playbackThread = g_thread_create( (GThreadFunc) playbackThread, self, TRUE, NULL );

    return 0;
}

static void
AlsaPlayer_dealloc( py_obj_AlsaPlayer *self ) {
    if( self->mutex != NULL && self->cond != NULL ) {
        g_mutex_lock( self->mutex );
        self->quit = true;
        g_cond_signal( self->cond );
        g_mutex_unlock( self->mutex );
    }
    else
        self->quit = true;

    Py_CLEAR( self->audioSource.csource );
    Py_CLEAR( self->audioSource.source );

    if( self->playbackThread != NULL )
        g_thread_join( self->playbackThread );

    if( self->pcmDevice != NULL ) {
        snd_pcm_close( self->pcmDevice );
        self->pcmDevice = NULL;
    }

    if( self->mutex != NULL ) {
        g_mutex_free( self->mutex );
        self->mutex = NULL;
    }

    if( self->cond != NULL ) {
        g_cond_free( self->cond );
        self->cond = NULL;
    }

    self->ob_type->tp_free( (PyObject*) self );
}

static int64_t
_getPresentationTime( py_obj_AlsaPlayer *self ) {
    if( self->stop )
        return self->seekTime;

    snd_pcm_uframes_t avail;
    snd_htimestamp_t tstamp;

    snd_pcm_htimestamp( self->pcmDevice, &avail, &tstamp );

    int64_t pcmTime = (int64_t) tstamp.tv_sec * INT64_C(1000000000) + (int64_t) tstamp.tv_nsec;

    return (pcmTime - self->baseTime) + self->seekTime;
}

static void
_getSpeed( py_obj_AlsaPlayer *self, rational *result ) {
    g_mutex_lock( self->mutex );
    result->n = self->stop ? 0 : 1;
    result->d = 1;
    g_mutex_unlock( self->mutex );
}

static PresentationClockFuncs sourceFuncs = {
    .getPresentationTime = (clock_getPresentationTimeFunc) _getPresentationTime,
    .getSpeed = (clock_getSpeedFunc) _getSpeed
};

static PyObject *pysourceFuncs;

static PyObject *
AlsaPlayer_getFuncs( py_obj_AlsaPlayer *self, void *closure ) {
    return pysourceFuncs;
}

static PyGetSetDef AlsaPlayer_getsetters[] = {
    { "_presentationClockFuncs", (getter) AlsaPlayer_getFuncs, NULL, "Presentation clock C API." },
    { NULL }
};

static PyObject *
AlsaPlayer_getPresentationTime( py_obj_AlsaPlayer *self ) {
    return Py_BuildValue( "L", _getPresentationTime( self ) );
}

static PyObject *
AlsaPlayer_stop( py_obj_AlsaPlayer *self ) {
    g_mutex_lock( self->mutex );
    self->seekTime = _getPresentationTime( self );
    self->stop = true;

    self->nextSample = getTimeFrame( &self->rate, self->seekTime );
    self->seekTime = getFrameTime( &self->rate, self->nextSample );

    g_mutex_unlock( self->mutex );

    Py_RETURN_NONE;
}

static PyObject *
AlsaPlayer_play( py_obj_AlsaPlayer *self ) {
    g_mutex_lock( self->mutex );
    self->stop = false;

    // The playback thread will grab the correct base time; we'll go ahead
    // and set an approximate value here
    snd_pcm_uframes_t avail;
    snd_htimestamp_t tstamp;
    int error;

    if( (error = snd_pcm_htimestamp( self->pcmDevice, &avail, &tstamp )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Failed to obtain current device time: %s", snd_strerror( error ) );
        return NULL;
    }

    self->baseTime = (int64_t) tstamp.tv_sec * INT64_C(1000000000) + (int64_t) tstamp.tv_nsec;
    g_cond_signal( self->cond );
    g_mutex_unlock( self->mutex );

    Py_RETURN_NONE;
}

static PyMethodDef AlsaPlayer_methods[] = {
    { "play", (PyCFunction) AlsaPlayer_play, METH_NOARGS,
        "Plays audio from the source." },
    { "stop", (PyCFunction) AlsaPlayer_stop, METH_NOARGS,
        "Stops playing audio from the source." },
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
    .tp_methods = AlsaPlayer_methods,
    .tp_getset = AlsaPlayer_getsetters
};

NOEXPORT void init_AlsaPlayer( PyObject *module ) {
    if( PyType_Ready( &py_type_AlsaPlayer ) < 0 )
        return;

    Py_INCREF( &py_type_AlsaPlayer );
    PyModule_AddObject( module, "AlsaPlayer", (PyObject *) &py_type_AlsaPlayer );

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );
}

