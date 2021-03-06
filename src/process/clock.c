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

typedef struct {
    PyObject_HEAD
    PresentationClockHolder holder;
    PyObject *callable, *data;
    void *handle;
} py_obj_ClockCallbackHandle;

static PyObject *
ClockCallbackHandle_unregister( py_obj_ClockCallbackHandle *self, PyObject *args ) {
    self->holder.source.funcs->unregister_callback( self->holder.source.obj, self->handle );

    Py_CLEAR( self->callable );
    Py_CLEAR( self->data );

    py_presentation_clock_take_source( NULL, &self->holder );

    Py_RETURN_NONE;
}

static void
ClockCallbackHandle_dealloc( py_obj_ClockCallbackHandle *self ) {
    Py_CLEAR( self->callable );
    Py_CLEAR( self->data );

    py_presentation_clock_take_source( NULL, &self->holder );

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static PyMethodDef ClockCallbackHandle_methods[] = {
    { "unregister", (PyCFunction) ClockCallbackHandle_unregister, METH_NOARGS,
        "Unregister this callback." },
    { NULL }
};

static PyTypeObject py_type_ClockCallbackHandle = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.ClockCallbackHandle",
    .tp_basicsize = sizeof(py_obj_ClockCallbackHandle),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_methods = ClockCallbackHandle_methods,
    .tp_dealloc = (destructor) ClockCallbackHandle_dealloc,
};

static PyObject *
PresentationClock_get_presentation_time( PyObject *self, PyObject *args ) {
    PresentationClockHolder holder = { { NULL } };

    if( !py_presentation_clock_take_source( self, &holder ) )
        return NULL;

    PyObject *result = Py_BuildValue( "L", holder.source.funcs->getPresentationTime( holder.source.obj ) );
    py_presentation_clock_take_source( NULL, &holder );

    return result;
}

static PyObject *
PresentationClock_get_speed( PyObject *self, PyObject *args ) {
    PresentationClockHolder holder = { { NULL } };

    if( !py_presentation_clock_take_source( self, &holder ) )
        return NULL;

    rational result;
    holder.source.funcs->getSpeed( holder.source.obj, &result );
    py_presentation_clock_take_source( NULL, &holder );

    return py_make_rational( &result );
}

static void
ClockCallbackHandle_callback( py_obj_ClockCallbackHandle *self, rational *speed, int64_t time ) {
    // Make sure we've got the GIL before we proceed
    PyGILState_STATE state = PyGILState_Ensure();

    PyObject *speed_obj = py_make_rational( speed );

    if( !speed_obj ) {
        g_warning( "In ClockCallbackHandle_callback:\n" );

        PyErr_Print();
        return;
    }

    PyObject *result = PyObject_CallFunction( self->callable, "OLO",
        speed_obj, time, self->data );

    Py_DECREF(speed_obj);

    if( result )
        Py_DECREF(result);
    else
        PyErr_Print();

    PyGILState_Release( state );
}

static void
ClockCallbackHandle_deref( py_obj_ClockCallbackHandle *self ) {
    Py_DECREF(self);
}

static PyObject *
PresentationClock_register_callback( PyObject *self, PyObject *args, PyObject *kw ) {
    static char *kwlist[] = { "func", "data", NULL };
    PyObject *callback, *data;

    if( !PyArg_ParseTupleAndKeywords( args, kw, "OO", kwlist,
            &callback, &data ) )
        return NULL;

    py_obj_ClockCallbackHandle *handle = PyObject_New( py_obj_ClockCallbackHandle, &py_type_ClockCallbackHandle );

    if( !handle )
        return NULL;

    handle->holder.source.obj = NULL;
    handle->holder.csource = NULL;
    handle->callable = NULL;
    handle->data = NULL;

    if( !py_presentation_clock_take_source( self, &handle->holder ) ) {
        Py_DECREF(handle);
        return NULL;
    }

    Py_INCREF(callback);
    Py_INCREF(data);
    Py_INCREF(handle);

    handle->callable = callback;
    handle->data = data;
    handle->handle = handle->holder.source.funcs->register_callback( handle->holder.source.obj,
        (clock_callback_func) ClockCallbackHandle_callback, handle, (GDestroyNotify) ClockCallbackHandle_deref );

    return (PyObject *) handle;
}

static PyMethodDef PresentationClock_methods[] = {
    { "get_presentation_time", (PyCFunction) PresentationClock_get_presentation_time, METH_NOARGS,
        "Get the presentation time in nanoseconds.\n"
        "\n"
        "time = clock.get_presentation_time()" },
    { "get_speed", (PyCFunction) PresentationClock_get_speed, METH_NOARGS,
        "Get the current speed of the clock as a fraction.\n"
        "\n"
        "speed = clock.get_speed()" },
    { "register_callback", (PyCFunction) PresentationClock_register_callback, METH_VARARGS | METH_KEYWORDS,
        "Register a clock-notification callback.\n"
        "\n"
        "handle = clock.register_callback(func, data)\n"
        "\n"
        "func - Function to call, of the form func(speed, time, data)\n"
        "data - Optional data to pass to the function." },
    { NULL }
};

EXPORT PyTypeObject py_type_PresentationClock = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.PresentationClock",
    .tp_basicsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_methods = PresentationClock_methods,
};

typedef struct __tag_callback_info {
    void *data;
    clock_callback_func callback;
    GDestroyNotify notify;
    struct __tag_callback_info *next;
} callback_info;

EXPORT bool py_presentation_clock_take_source( PyObject *source, PresentationClockHolder *holder ) {
    Py_CLEAR( holder->source.obj );
    Py_CLEAR( holder->csource );
    holder->source.funcs = NULL;

    if( source == NULL || source == Py_None )
        return true;

    Py_INCREF( source );
    holder->source.obj = source;
    holder->csource = PyObject_GetAttrString( source, PRESENTATION_CLOCK_FUNCS );

    if( !PyCapsule_IsValid( holder->csource, PRESENTATION_CLOCK_FUNCS ) ) {
        Py_CLEAR( holder->source.obj );
        PyErr_SetString( PyExc_Exception, "The source didn't have an acceptable " PRESENTATION_CLOCK_FUNCS " attribute." );
        return false;
    }

    holder->source.funcs = (PresentationClockFuncs*) PyCapsule_GetPointer(
        holder->csource, PRESENTATION_CLOCK_FUNCS );
    g_assert(holder->source.funcs);

    return true;
}

typedef struct {
    PyObject_HEAD

    GMutex mutex;
    int64_t seekTime, baseTime;
    ClockRegions regions;
    rational speed;

    GRWLock callback_lock;
    callback_info *callbacks;
} py_obj_SystemPresentationClock;

/***************** SystemPresentationClock *********/
static int
SystemPresentationClock_init( py_obj_SystemPresentationClock *self, PyObject *args, PyObject *kwds ) {
    self->seekTime = INT64_C(0);
    self->speed.n = 0;
    self->speed.d = 1;
    self->baseTime = gettime();
    self->regions.playbackMin = 0;
    self->regions.playbackMax = 0;
    self->regions.loopMin = 0;
    self->regions.loopMax = -1;
    self->regions.flags = 0;
    self->callbacks = NULL;
    g_mutex_init( &self->mutex );
    g_rw_lock_init( &self->callback_lock );

    return 0;
}

static void
SystemPresentationClock_dealloc( py_obj_SystemPresentationClock *self ) {
    // Free the callback list
    while( self->callbacks ) {
        callback_info *info = self->callbacks;
        self->callbacks = info->next;

        if( info->notify )
            info->notify( info->data );

        g_slice_free( callback_info, info );
    }

    g_mutex_clear( &self->mutex );
    g_rw_lock_clear( &self->callback_lock );

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static void
_set( py_obj_SystemPresentationClock *self, int64_t seek_time, rational *speed ) {
    g_mutex_lock( &self->mutex );
    self->baseTime = gettime();
    self->seekTime = seek_time;
    self->speed = *speed;
    g_mutex_unlock( &self->mutex );

    g_rw_lock_reader_lock( &self->callback_lock );
    for( callback_info *ptr = self->callbacks; ptr != NULL; ptr = ptr->next ) {
        ptr->callback( ptr->data, speed, seek_time );
    }
    g_rw_lock_reader_unlock( &self->callback_lock );
}

static int64_t
_getPresentationTime( py_obj_SystemPresentationClock *self ) {
    g_mutex_lock( &self->mutex );
    int64_t seekTime = self->seekTime;

    if( self->speed.n == 0 ) {
        g_mutex_unlock( &self->mutex );
        return seekTime;
    }

    int64_t elapsed = (gettime() - self->baseTime) * self->speed.n;

    if( self->speed.d != 1 )
        elapsed /= self->speed.d;

    int64_t currentTime = seekTime + elapsed;

    // BJC: This code is broken and will need to be fixed
/*    if( self->speed.n > 0 ) {
        if( currentTime > self->regions.playbackMax ) {
            self->baseTime = gettime();
            self->seekTime = self->regions.playbackMax;
            self->speed.n = 0;
            self->speed.d = 1;
        }
        else if( self->regions.loopMin <= self->regions.loopMax && seekTime <= self->regions.loopMax ) {
            // We could be looping right now
            if( currentTime > self->regions.loopMax ) {
                currentTime = (seekTime - self->regions.loopMin) +
                    elapsed % (self->regions.loopMax - self->regions.loopMin + 1);
            }
        }
    }
    else {
        // Going backwards, reverse situation
        if( currentTime < self->regions.playbackMin ) {
            self->baseTime = gettime();
            self->seekTime = self->regions.playbackMin;
            self->speed.n = 0;
            self->speed.d = 1;
        }
        else if( self->regions.loopMin <= self->regions.loopMax && seekTime >= self->regions.loopMin ) {
            // We could be looping right now
            if( currentTime < self->regions.loopMin ) {
                currentTime = (self->regions.loopMin - seekTime) +
                    elapsed % (self->regions.loopMax - self->regions.loopMin + 1);
            }
        }
    }*/

    g_mutex_unlock( &self->mutex );

    return currentTime;
}

static void
_getSpeed( py_obj_SystemPresentationClock *self, rational *result ) {
    g_mutex_lock( &self->mutex );
    *result = self->speed;
    g_mutex_unlock( &self->mutex );
}

static PyObject *
SystemPresentationClock_set( py_obj_SystemPresentationClock *self, PyObject *args ) {
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

static PyObject *
SystemPresentationClock_play( py_obj_SystemPresentationClock *self, PyObject *args ) {
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
SystemPresentationClock_seek( py_obj_SystemPresentationClock *self, PyObject *args ) {
    int64_t time;

    if( !PyArg_ParseTuple( args, "L", &time ) )
        return NULL;

    _set( self, time, &self->speed );

    Py_RETURN_NONE;
}

static PyObject *
SystemPresentationClock_stop( py_obj_SystemPresentationClock *self ) {
    rational rate = { 0, 1 };
    _set( self, _getPresentationTime( self ), &rate );

    Py_RETURN_NONE;
}

static void *
_register_callback( py_obj_SystemPresentationClock *self, clock_callback_func callback, void *data, GDestroyNotify notify ) {
    g_assert( callback );

    callback_info *info = g_slice_new( callback_info );

    info->data = data;
    info->callback = callback;
    info->notify = notify;

    g_rw_lock_writer_lock( &self->callback_lock );
    info->next = self->callbacks;
    self->callbacks = info;
    g_rw_lock_writer_unlock( &self->callback_lock );

    return self->callbacks;
}

static void
_unregister_callback( py_obj_SystemPresentationClock *self, callback_info *info ) {
    // Unlink it
    g_rw_lock_writer_lock( &self->callback_lock );
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
    g_rw_lock_writer_unlock( &self->callback_lock );

    // Call the GDestroyNotify to let them know we don't hold this anymore
    if( info->notify )
        info->notify( info->data );

    g_slice_free( callback_info, info );
}

static PyMethodDef SystemPresentationClock_methods[] = {
    { "play", (PyCFunction) SystemPresentationClock_play, METH_VARARGS,
        "Starts the clock at the current spot." },
    { "set", (PyCFunction) SystemPresentationClock_set, METH_VARARGS,
        "Sets the speed and current time." },
    { "stop", (PyCFunction) SystemPresentationClock_stop, METH_NOARGS,
        "Stops the clock." },
    { "seek", (PyCFunction) SystemPresentationClock_seek, METH_VARARGS,
        "Sets the current time." },
    { NULL }
};

static PresentationClockFuncs sourceFuncs = {
    .getPresentationTime = (clock_getPresentationTimeFunc) _getPresentationTime,
    .getSpeed = (clock_getSpeedFunc) _getSpeed,
    .register_callback = (clock_register_callback_func) _register_callback,
    .unregister_callback = (clock_unregister_callback_func) _unregister_callback
};

static PyObject *pysourceFuncs;

static PyObject *
SystemPresentationClock_getFuncs( py_obj_SystemPresentationClock *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef SystemPresentationClock_getsetters[] = {
    { PRESENTATION_CLOCK_FUNCS, (getter) SystemPresentationClock_getFuncs, NULL, "Presentation clock C API." },
    { NULL }
};

static PyTypeObject py_type_SystemPresentationClock = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.SystemPresentationClock",
    .tp_basicsize = sizeof(py_obj_SystemPresentationClock),
    .tp_base = &py_type_PresentationClock,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) SystemPresentationClock_dealloc,
    .tp_init = (initproc) SystemPresentationClock_init,
    .tp_methods = SystemPresentationClock_methods,
    .tp_getset = SystemPresentationClock_getsetters
};


void init_SystemPresentationClock( PyObject *module ) {
    if( PyType_Ready( &py_type_PresentationClock ) < 0 )
        return;

    if( PyType_Ready( &py_type_SystemPresentationClock ) < 0 )
        return;

    Py_INCREF( &py_type_SystemPresentationClock );
    PyModule_AddObject( module, "SystemPresentationClock", (PyObject *) &py_type_SystemPresentationClock );

    pysourceFuncs = PyCapsule_New( &sourceFuncs, PRESENTATION_CLOCK_FUNCS, NULL );

    if( PyType_Ready( &py_type_ClockCallbackHandle ) < 0 )
        return;
}

