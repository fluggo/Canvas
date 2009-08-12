
#include "framework.h"
#include "clock.h"
#include <gtk/gtk.h>

int64_t gettime() {
    struct timespec time;
    clock_gettime( CLOCK_MONOTONIC, &time );

    return ((int64_t) time.tv_sec) * INT64_C(1000000000) + (int64_t) time.tv_nsec;
}

bool takePresentationClock( PyObject *source, PresentationClockHolder *holder ) {
    Py_CLEAR( holder->source );
    Py_CLEAR( holder->csource );
    holder->funcs = NULL;

    if( source == NULL || source == Py_None )
        return true;

    Py_INCREF( source );
    holder->source = source;
    holder->csource = PyObject_GetAttrString( source, "_presentationClockFuncs" );

    if( holder->csource == NULL ) {
        Py_CLEAR( holder->source );
        PyErr_SetString( PyExc_Exception, "The source didn't have an acceptable _presentationClockFuncs attribute." );
        return false;
    }

    holder->funcs = (PresentationClockFuncs*) PyCObject_AsVoidPtr( holder->csource );

    return true;
}

typedef struct {
    PyObject_HEAD

    GMutex *mutex;
    int64_t seekTime, baseTime;
    ClockRegions regions;
    rational speed;
} py_obj_SystemPresentationClock;

/***************** SystemPresentationClock *********/
static int
SystemPresentationClock_init( py_obj_SystemPresentationClock *self, PyObject *args, PyObject *kwds ) {
    self->seekTime = INT64_C(0);
    self->speed.n = 0;
    self->speed.d = 1;
    self->mutex = g_mutex_new();
    self->baseTime = gettime();
    self->regions.playbackMin = 0;
    self->regions.playbackMax = 0;
    self->regions.loopMin = 0;
    self->regions.loopMax = -1;
    self->regions.flags = 0;

    return 0;
}

static void
SystemPresentationClock_dealloc( py_obj_SystemPresentationClock *self ) {
    g_mutex_free( self->mutex );
    self->ob_type->tp_free( (PyObject*) self );
}

static void
_set( py_obj_SystemPresentationClock *self, int64_t seekTime, rational *speed ) {
    g_mutex_lock( self->mutex );
    self->baseTime = gettime();
    self->seekTime = seekTime;
    self->speed = *speed;
    g_mutex_unlock( self->mutex );
}

static int64_t
_getPresentationTime( py_obj_SystemPresentationClock *self ) {
    g_mutex_lock( self->mutex );
    int64_t seekTime = self->seekTime;

    if( self->speed.n == 0 ) {
        g_mutex_unlock( self->mutex );
        return seekTime;
    }

    int64_t elapsed = (gettime() - self->baseTime) * self->speed.n;

    if( self->speed.d != 1 )
        elapsed /= self->speed.d;

    int64_t currentTime = seekTime + elapsed;

    if( self->speed.n > 0 ) {
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
    }

    g_mutex_unlock( self->mutex );

    return currentTime;
}

static PyObject *
SystemPresentationClock_getPresentationTime( py_obj_SystemPresentationClock *self ) {
    return Py_BuildValue( "L", _getPresentationTime( self ) );
}

static void
_getSpeed( py_obj_SystemPresentationClock *self, rational *result ) {
    g_mutex_lock( self->mutex );
    *result = self->speed;
    g_mutex_unlock( self->mutex );
}

static PyObject *
SystemPresentationClock_set( py_obj_SystemPresentationClock *self, PyObject *args ) {
    PyObject *rateObj;
    rational rate;
    int64_t time;

    if( !PyArg_ParseTuple( args, "OL", &rateObj, &time ) )
        return NULL;

    if( !parseRational( rateObj, &rate ) )
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

    if( !parseRational( rateObj, &rate ) )
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

static PyMethodDef SystemPresentationClock_methods[] = {
    { "play", (PyCFunction) SystemPresentationClock_play, METH_VARARGS,
        "Starts the clock at the current spot." },
    { "set", (PyCFunction) SystemPresentationClock_set, METH_VARARGS,
        "Sets the speed and current time." },
    { "stop", (PyCFunction) SystemPresentationClock_stop, METH_NOARGS,
        "Stops the clock." },
    { "seek", (PyCFunction) SystemPresentationClock_seek, METH_VARARGS,
        "Sets the current time." },
    { "getPresentationTime", (PyCFunction) SystemPresentationClock_getPresentationTime, METH_NOARGS,
        "Gets the current presentation time in nanoseconds." },
    { NULL }
};

static PresentationClockFuncs sourceFuncs = {
    .getPresentationTime = (clock_getPresentationTimeFunc) _getPresentationTime,
    .getSpeed = (clock_getSpeedFunc) _getSpeed
};

static PyObject *pysourceFuncs;

static PyObject *
SystemPresentationClock_getFuncs( py_obj_SystemPresentationClock *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef SystemPresentationClock_getsetters[] = {
    { "_presentationClockFuncs", (getter) SystemPresentationClock_getFuncs, NULL, "Presentation clock C API." },
    { NULL }
};

static PyTypeObject py_type_SystemPresentationClock = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.SystemPresentationClock",    // tp_name
    sizeof(py_obj_SystemPresentationClock),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) SystemPresentationClock_dealloc,
    .tp_init = (initproc) SystemPresentationClock_init,
    .tp_methods = SystemPresentationClock_methods,
    .tp_getset = SystemPresentationClock_getsetters
};


NOEXPORT void init_SystemPresentationClock( PyObject *module ) {
    if( PyType_Ready( &py_type_SystemPresentationClock ) < 0 )
        return;

    Py_INCREF( &py_type_SystemPresentationClock );
    PyModule_AddObject( module, "SystemPresentationClock", (PyObject *) &py_type_SystemPresentationClock );

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );
}

