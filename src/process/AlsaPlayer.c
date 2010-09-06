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
#include <asoundlib.h>

typedef struct __tag_callback_info {
    void *data;
    clock_callback_func callback;
    GDestroyNotify notify;
    struct __tag_callback_info *next;
} callback_info;

typedef struct {
    PyObject_HEAD

    int nextSample;
    int64_t seekTime, baseTime;
    AudioSourceHolder audioSource;
    snd_pcm_t *pcmDevice;
    GThread *playbackThread;
    GMutex *mutex, *configMutex;
    GCond *cond;
    bool quit, stop;
    rational rate, playSpeed;
    int bufferSize, channelCount;
    float *inBuffer;
    void *outBuffer;
    snd_pcm_hw_params_t *hwParams;

    GStaticRWLock callback_lock;
    callback_info *callbacks;
} py_obj_AlsaPlayer;

static int64_t _getPresentationTime( py_obj_AlsaPlayer *self );

static gpointer
playbackThread( py_obj_AlsaPlayer *self ) {
    for( ;; ) {
        g_mutex_lock( self->mutex );

        if( self->stop )
            snd_pcm_drop( self->pcmDevice );

        while( !self->quit && self->stop )
            g_cond_wait( self->cond, self->mutex );

        if( snd_pcm_state( self->pcmDevice ) == SND_PCM_STATE_SETUP )
            snd_pcm_prepare( self->pcmDevice );

        //printf( "%s\n", snd_pcm_state_name( snd_pcm_state( self->pcmDevice ) ) );

        if( self->quit ) {
            g_mutex_unlock( self->mutex );
            break;
        }

        rational speed = self->playSpeed;
        rational rate = self->rate;
        int nextSample = self->nextSample;
        float *inptr = self->inBuffer;
        void *outptr = self->outBuffer;
        int hwCount = min(self->bufferSize, self->bufferSize * speed.d / abs(speed.n));
        int swCount = min(self->bufferSize, self->bufferSize * abs(speed.n) / speed.d);

        // Grab the current buffer/period size
        snd_pcm_uframes_t hwBufferSize, hwPeriodSize;
        snd_pcm_get_params( self->pcmDevice, &hwBufferSize, &hwPeriodSize );

        audio_frame frame;
        frame.channels = self->channelCount;
        frame.data = self->inBuffer;

        if( speed.n > 0 ) {
            frame.full_min_sample = nextSample;
            frame.full_max_sample = nextSample + swCount - 1;
            self->nextSample += swCount;
        }
        else {
            frame.full_min_sample = nextSample - swCount + 1;
            frame.full_max_sample = nextSample;
            self->nextSample -= swCount;
        }

        frame.current_min_sample = frame.full_min_sample;
        frame.current_max_sample = frame.full_max_sample;

        g_mutex_unlock( self->mutex );

        self->audioSource.source.funcs->getFrame( self->audioSource.source.obj, &frame );

        // Convert speed differences
        if( speed.n == 1 && speed.d == 1 ) {
            // As long as the output is float, we can use the original buffer
            outptr = inptr;
        }
        else if( speed.n > 0 ) {
            for( int i = 0; i < hwCount; i++ ) {
                for( int ch = 0; ch < frame.channels; ch++ ) {
                    ((float *) outptr)[i * frame.channels + ch] =
                        inptr[(i * speed.n / speed.d) * frame.channels + ch];
                }
            }
        }
        else {
            for( int i = 0; i < hwCount; i++ ) {
                for( int ch = 0; ch < frame.channels; ch++ ) {
                    ((float *) outptr)[(hwCount - i - 1) * frame.channels + ch] =
                        inptr[(i * -speed.n / speed.d) * frame.channels + ch];
                }
            }
        }

        // Do this next part under a lock, because the
        // Python thread may want to stop the device/change the config
        g_mutex_lock( self->configMutex );

        if( self->stop ) {
            g_mutex_unlock( self->configMutex );
            continue;
        }

        while( hwCount > 0 ) {
            int error;

            // BJC: I like someone who makes my life easy:
            // the ALSA API here is self-limiting
            error = snd_pcm_writei( self->pcmDevice, outptr, hwCount );

            if( error == -EAGAIN )
                continue;

            if( error == -EPIPE ) {
                // Underrun!
                printf("ALSA playback underrun\n" );
                snd_pcm_prepare( self->pcmDevice );
                self->nextSample = getTimeFrame( &rate, _getPresentationTime( self ) );
                break;
            }

            outptr += error * frame.channels * sizeof(float);
            hwCount -= error;
        }

        // Reset the clock so that it stays in sync
        snd_htimestamp_t tstamp;
        snd_pcm_uframes_t avail;
        snd_pcm_htimestamp( self->pcmDevice, &avail, &tstamp );

        g_mutex_lock( self->mutex );
        self->baseTime = gettime();

        if( speed.n > 0 )
            self->seekTime = getFrameTime( &rate, self->nextSample - (hwBufferSize - avail) );
        else
            self->seekTime = getFrameTime( &rate, self->nextSample + (hwBufferSize - avail) );

        //printf( "nextSample: %d, hwBufferSize: %d, avail: %d\n", self->nextSample, hwBufferSize, avail );
        g_mutex_unlock( self->mutex );

        g_mutex_unlock( self->configMutex );
    }

    snd_pcm_drop( self->pcmDevice );

    return NULL;
}

static bool _setConfig( py_obj_AlsaPlayer *self, unsigned int *ratePtr, unsigned int *channelsPtr ) {
    // This is a Python-only method; we include a raw version here
    // for use inside the init method

    int error;

    unsigned int channels = (channelsPtr && *channelsPtr) ? *channelsPtr : 2,
        rate = (ratePtr && *ratePtr) ? *ratePtr : 48000;

    if( !self->hwParams ) {
        self->hwParams = PyMem_Malloc( snd_pcm_hw_params_sizeof() );

        if( self->hwParams == NULL ) {
            PyErr_NoMemory();
            return false;
        }
    }
    else {
        if( (error = snd_pcm_drop( self->pcmDevice )) < 0 ) {
            PyErr_Format( PyExc_Exception, "Could not stop device: %s", snd_strerror( error ) );
            return false;
        }

        // Existing config; swipe numbers from it if we can
        if( channelsPtr && !*channelsPtr ) {
            if( (error = snd_pcm_hw_params_get_channels( self->hwParams, &channels )) < 0 ) {
                PyErr_Format( PyExc_Exception, "Could not read channels from existing config: %s", snd_strerror( error ) );
                return false;
            }
        }

        if( ratePtr && !*ratePtr ) {
            unsigned int num, den;

            if( (error = snd_pcm_hw_params_get_rate_numden( self->hwParams, &num, &den )) < 0 ) {
                PyErr_Format( PyExc_Exception, "Could not read rate from existing config: %s", snd_strerror( error ) );
                return false;
            }

            rate = num / den;
        }
    }

    if( (error = snd_pcm_hw_params_any( self->pcmDevice, self->hwParams )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Could not open configuration for playback: %s", snd_strerror( error ) );
        return false;
    }

    do {
        // Whatever happens in here, self->hwParams needs to try to have the current HW state by the end
        if( (error = snd_pcm_hw_params_set_access( self->pcmDevice, self->hwParams, SND_PCM_ACCESS_RW_INTERLEAVED )) < 0 ) {
            PyErr_Format( PyExc_Exception, "Failed to set interleaved access: %s", snd_strerror( error ) );
            break;
        }

        // Set stereo channels for now
        if( (error = snd_pcm_hw_params_set_channels_near( self->pcmDevice, self->hwParams, &channels )) < 0 ) {
            PyErr_Format( PyExc_Exception, "Failed to set channel count: %s", snd_strerror( error ) );
            break;
        }

        // Grab what should be an easy-to-find format
        if( (error = snd_pcm_hw_params_set_format( self->pcmDevice, self->hwParams, SND_PCM_FORMAT_FLOAT )) < 0 ) {
            PyErr_Format( PyExc_Exception, "Failed to set sample format: %s", snd_strerror( error ) );
            break;
        }

        if( (error = snd_pcm_hw_params_set_rate_near( self->pcmDevice, self->hwParams, &rate, NULL )) < 0 ) {
            PyErr_Format( PyExc_Exception, "Failed to set sample rate: %s", snd_strerror( error ) );
            break;
        }

        if( (error = snd_pcm_hw_params( self->pcmDevice, self->hwParams )) < 0 ) {
            PyErr_Format( PyExc_Exception, "Failed to write parameter set: %s", snd_strerror( error ) );
            break;
        }

        // Read back config
        if( (error = snd_pcm_hw_params_current( self->pcmDevice, self->hwParams )) < 0 ) {
            PyErr_Format( PyExc_Exception, "Could not read current config: %s", snd_strerror( error ) );
            return false;
        }

        self->rate.n = rate;
        self->rate.d = 1;
        self->channelCount = channels;

        if( ratePtr )
            *ratePtr = rate;

        if( channelsPtr )
            *channelsPtr = channels;

        return true;
    } while( 0 );

    // Emergency get hw state
    snd_pcm_hw_params_current( self->pcmDevice, self->hwParams );
    snd_pcm_prepare( self->pcmDevice );
    return false;
}

static int
AlsaPlayer_init( py_obj_AlsaPlayer *self, PyObject *args, PyObject *kw ) {
    if( !g_thread_supported() )
        g_thread_init( NULL );

    PyObject *frameSource = NULL;

    unsigned int rate = 0, channels = 0;
    static char *kwlist[] = { "rate", "channels", "source", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "|IIO", kwlist,
            &rate, &channels, &frameSource ) )
        return -1;

    if( !py_audio_takeSource( frameSource, &self->audioSource ) )
        return -1;

    int error;
    const char *deviceName = "default";

    if( (error = snd_pcm_open( &self->pcmDevice, "default", SND_PCM_STREAM_PLAYBACK, 0 )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Could not open PCM device %s: %s", deviceName, snd_strerror( error ) );
        return -1;
    }

    if( !_setConfig( self, &rate, &channels ) )
        return -1;

    self->mutex = g_mutex_new();
    self->configMutex = g_mutex_new();
    self->cond = g_cond_new();
    self->stop = true;
    self->bufferSize = 1024;
    self->inBuffer = PyMem_Malloc( self->bufferSize * self->channelCount * sizeof(float) );

    if( self->inBuffer == NULL ) {
        PyErr_NoMemory();
        return -1;
    }

    self->outBuffer = PyMem_Malloc( self->bufferSize * self->channelCount * sizeof(float) );

    if( self->outBuffer == NULL ) {
        PyErr_NoMemory();
        return -1;
    }

    self->playbackThread = g_thread_create( (GThreadFunc) playbackThread, self, TRUE, NULL );

    self->callbacks = NULL;
    g_static_rw_lock_init( &self->callback_lock );

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

    py_audio_takeSource( NULL, &self->audioSource );

    if( self->playbackThread != NULL )
        g_thread_join( self->playbackThread );

    if( self->inBuffer != NULL ) {
        PyMem_Free( self->inBuffer );
        self->inBuffer = NULL;
    }

    if( self->outBuffer != NULL ) {
        PyMem_Free( self->outBuffer );
        self->inBuffer = NULL;
    }

    if( self->hwParams != NULL ) {
        PyMem_Free( self->hwParams );
        self->hwParams = NULL;
    }

    if( self->pcmDevice != NULL ) {
        snd_pcm_close( self->pcmDevice );
        self->pcmDevice = NULL;
    }

    if( self->configMutex ) {
        g_mutex_free( self->configMutex );
        self->configMutex = NULL;
    }

    if( self->mutex ) {
        g_mutex_free( self->mutex );
        self->mutex = NULL;
    }

    if( self->cond != NULL ) {
        g_cond_free( self->cond );
        self->cond = NULL;
    }

    // Free the callback list
    while( self->callbacks ) {
        callback_info *info = self->callbacks;
        self->callbacks = info->next;

        if( info->notify )
            info->notify( info->data );

        g_slice_free( callback_info, info );
    }

    g_static_rw_lock_free( &self->callback_lock );

    self->ob_type->tp_free( (PyObject*) self );
}

static PyObject *AlsaPlayer_setConfig( py_obj_AlsaPlayer *self, PyObject *args, PyObject *kw ) {
    unsigned int channels = 0, rate = 0;

    static char *kwlist[] = { "rate", "channels", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "|II", kwlist,
            &rate, &channels ) )
        return NULL;

    g_mutex_lock( self->configMutex );
    if( !_setConfig( self, &rate, &channels ) ) {
        g_mutex_unlock( self->configMutex );
        return NULL;
    }
    g_mutex_unlock( self->configMutex );

    return Py_BuildValue( "II", rate, channels );
}

static int64_t
_getPresentationTime_nolock( py_obj_AlsaPlayer *self ) {
    if( self->stop )
        return self->seekTime;

    int64_t elapsed = (gettime() - self->baseTime) * self->playSpeed.n;
    int64_t seekTime = self->seekTime;
    unsigned int d = self->playSpeed.d;

    if( d == 1 )
        return elapsed + seekTime;
    else
        return elapsed / d + seekTime;
}

static int64_t
_getPresentationTime( py_obj_AlsaPlayer *self ) {
    g_mutex_lock( self->mutex );
    int64_t result = _getPresentationTime_nolock( self );
    g_mutex_unlock( self->mutex );

    return result;
}

static void
_getSpeed( py_obj_AlsaPlayer *self, rational *result ) {
    g_mutex_lock( self->mutex );
    *result = self->playSpeed;
    g_mutex_unlock( self->mutex );
}

static void
_set( py_obj_AlsaPlayer *self, int64_t seek_time, rational *speed ) {
    g_mutex_lock( self->mutex );
    self->stop = (speed->n == 0);

    self->baseTime = gettime();
    self->seekTime = seek_time;
    self->playSpeed = *speed;
    self->nextSample = getTimeFrame( &self->rate, self->seekTime );
    self->seekTime = getFrameTime( &self->rate, self->nextSample );
    g_cond_signal( self->cond );
    g_mutex_unlock( self->mutex );

    // Call callbacks (taking the lock here *might* not be the best idea)
    g_static_rw_lock_reader_lock( &self->callback_lock );
    for( callback_info *ptr = self->callbacks; ptr != NULL; ptr = ptr->next ) {
        ptr->callback( ptr->data, speed, seek_time );
    }
    g_static_rw_lock_reader_unlock( &self->callback_lock );
}

static PyObject *
AlsaPlayer_set( py_obj_AlsaPlayer *self, PyObject *args ) {
    PyObject *rateObj;
    rational rate;
    int64_t time;

    if( !PyArg_ParseTuple( args, "OL", &rateObj, &time ) )
        return NULL;

    if( !py_parse_rational( rateObj, &rate ) )
        return NULL;

    _set( self, time, &rate );

    Py_RETURN_NONE;
}

static void *
_register_callback( py_obj_AlsaPlayer *self, clock_callback_func callback, void *data, GDestroyNotify notify ) {
    g_assert( callback );

    callback_info *info = g_slice_new( callback_info );

    info->data = data;
    info->callback = callback;
    info->notify = notify;

    g_static_rw_lock_writer_lock( &self->callback_lock );
    info->next = self->callbacks;
    self->callbacks = info;
    g_static_rw_lock_writer_unlock( &self->callback_lock );

    return self->callbacks;
}

static void
_unregister_callback( py_obj_AlsaPlayer *self, callback_info *info ) {
    // Unlink it
    g_static_rw_lock_writer_lock( &self->callback_lock );
    if( self->callbacks == info ) {
        self->callbacks = info->next;
    }
    else {
        for( callback_info *ptr = self->callbacks; ptr != NULL; ptr = ptr->next ) {
            if( ptr->next == info ) {
                ptr->next = info->next;
                break;
            }
        }
    }
    g_static_rw_lock_writer_unlock( &self->callback_lock );

    // Call the GDestroyNotify to let them know we don't hold this anymore
    if( info->notify )
        info->notify( info->data );

    g_slice_free( callback_info, info );
}

static PresentationClockFuncs sourceFuncs = {
    .getPresentationTime = (clock_getPresentationTimeFunc) _getPresentationTime,
    .getSpeed = (clock_getSpeedFunc) _getSpeed,
    .register_callback = (clock_register_callback_func) _register_callback,
    .unregister_callback = (clock_unregister_callback_func) _unregister_callback
};

static PyObject *pysourceFuncs;

static PyObject *
AlsaPlayer_getFuncs( py_obj_AlsaPlayer *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef AlsaPlayer_getsetters[] = {
    { PRESENTATION_CLOCK_FUNCS, (getter) AlsaPlayer_getFuncs, NULL, "Presentation clock C API." },
    { NULL }
};

static PyObject *
AlsaPlayer_stop( py_obj_AlsaPlayer *self ) {
    rational speed = { 0, 1 };
    _set( self, _getPresentationTime( self ), &speed );

    Py_RETURN_NONE;
}

static PyObject *
AlsaPlayer_play( py_obj_AlsaPlayer *self, PyObject *args ) {
    PyObject *rateObj;
    rational rate;

    if( !PyArg_ParseTuple( args, "O", &rateObj ) )
        return NULL;

    if( !py_parse_rational( rateObj, &rate ) )
        return NULL;

    _set( self, _getPresentationTime( self ), &rate );

    Py_RETURN_NONE;
}

static PyObject *
AlsaPlayer_seek( py_obj_AlsaPlayer *self, PyObject *args ) {
    int64_t time;

    if( !PyArg_ParseTuple( args, "L", &time ) )
        return NULL;

    _set( self, time, &self->playSpeed );

    Py_RETURN_NONE;
}

static PyMethodDef AlsaPlayer_methods[] = {
    { "set", (PyCFunction) AlsaPlayer_set, METH_VARARGS,
        "Sets the speed and current time." },
    { "seek", (PyCFunction) AlsaPlayer_seek, METH_VARARGS,
        "Sets the current time." },
    { "play", (PyCFunction) AlsaPlayer_play, METH_VARARGS,
        "Plays audio from the source starting at the current spot." },
    { "stop", (PyCFunction) AlsaPlayer_stop, METH_NOARGS,
        "Stops playing audio from the source." },
    { "set_config", (PyCFunction) AlsaPlayer_setConfig, METH_VARARGS | METH_KEYWORDS,
        "rate, channels = setConfig([rate = 48000, channels = 2]): Sets the configuration for this device." },
    { NULL }
};

static PyTypeObject py_type_AlsaPlayer = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.process.AlsaPlayer",    // tp_name
    sizeof(py_obj_AlsaPlayer),    // tp_basicsize
    .tp_base = &py_type_PresentationClock,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AlsaPlayer_dealloc,
    .tp_init = (initproc) AlsaPlayer_init,
    .tp_methods = AlsaPlayer_methods,
    .tp_getset = AlsaPlayer_getsetters
};

void init_AlsaPlayer( PyObject *module ) {
    if( PyType_Ready( &py_type_AlsaPlayer ) < 0 )
        return;

    Py_INCREF( &py_type_AlsaPlayer );
    PyModule_AddObject( module, "AlsaPlayer", (PyObject *) &py_type_AlsaPlayer );

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );
}

