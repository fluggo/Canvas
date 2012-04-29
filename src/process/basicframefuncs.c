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

static void
generic_dealloc( PyObject *self ) {
    self->ob_type->tp_free( (PyObject*) self );
}

#define DECLARE_FRAMEFUNC(name, deallocFunc)    \
    static PyTypeObject py_type_##name = { \
        PyVarObject_HEAD_INIT(NULL, 0) \
        .tp_name = "fluggo.media.process." #name, \
        .tp_basicsize = sizeof(py_obj_##name), \
        .tp_flags = Py_TPFLAGS_DEFAULT, \
        .tp_base = &py_type_FrameFunction, \
        .tp_new = PyType_GenericNew, \
        .tp_dealloc = (destructor) deallocFunc, \
        .tp_init = (initproc) name##_init, \
        .tp_getset = name##_getsetters \
    };

#define DECLARE_GETTER(name) \
    static PyObject *name##_pysourceFuncs; \
    \
    static PyObject * \
    name##_getFuncs( PyObject *self, void *closure ) { \
        Py_INCREF(name##_pysourceFuncs); \
        return name##_pysourceFuncs; \
    } \

#define DECLARE_GETSETTER(name) \
    { FRAME_FUNCTION_FUNCS, (getter) name##_getFuncs, NULL, "Frame function C API." }

#define DECLARE_DEFAULT_GETSETTERS(name) \
    static PyGetSetDef name##_getsetters[] = { \
        DECLARE_GETSETTER(name), \
        { NULL } \
    };

#define SETUP_FRAMEFUNC(name) \
    if( PyType_Ready( &py_type_##name ) < 0 ) \
        return; \
    \
    Py_INCREF( (PyObject*) &py_type_##name ); \
    PyModule_AddObject( module, #name, (PyObject *) &py_type_##name ); \
    \
    name##_pysourceFuncs = PyCapsule_New( &name##_sourceFuncs, FRAME_FUNCTION_FUNCS, NULL );


typedef struct {
    PyObject_HEAD
    float a, b;
} py_obj_LinearFrameFunc;

static int
LinearFrameFunc_init( py_obj_LinearFrameFunc *self, PyObject *args, PyObject *kwds ) {
    static char *kwlist[] = { "a", "b", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kwds, "ff", kwlist,
            &self->a, &self->b ) )
        return -1;

    return 0;
}

static void
LinearFrameFunc_getValues( py_obj_LinearFrameFunc *self, int count, double *frames, float (*outValues)[4] ) {
    for( int i = 0; i < count; i++ ) {
        outValues[i][0] = frames[i] * self->a + self->b;
        outValues[i][1] = 0.0;
        outValues[i][2] = 0.0;
        outValues[i][3] = 0.0;
    }
}

static FrameFunctionFuncs LinearFrameFunc_sourceFuncs = {
    0,
    .get_values = (framefunc_get_values_func) LinearFrameFunc_getValues
};

DECLARE_GETTER(LinearFrameFunc)
DECLARE_DEFAULT_GETSETTERS(LinearFrameFunc)
DECLARE_FRAMEFUNC(LinearFrameFunc, generic_dealloc)


typedef struct {
    PyObject_HEAD
    box2f start, end;
    double length;
} py_obj_LerpFunc;

static int
LerpFunc_init( py_obj_LerpFunc *self, PyObject *args, PyObject *kwds ) {
    static char *kwlist[] = { "start", "end", "length", NULL };
    PyObject *start_obj, *end_obj;

    if( !PyArg_ParseTupleAndKeywords( args, kwds, "OOd", kwlist, &start_obj, &end_obj, &self->length ) )
        return -1;

    if( self->length <= 0.0f ) {
        PyErr_SetString( PyExc_Exception, "length must be greater than 1." );
        return -1;
    }

    start_obj = PySequence_Fast( start_obj, "Expected a tuple or list for start." );

    if( !start_obj )
        return -1;

    float *a = &self->start.min.x;

    for( Py_ssize_t i = 0; i < 4; i++ ) {
        if( i < PySequence_Fast_GET_SIZE( start_obj ) )
            a[i] = PyFloat_AsDouble( PySequence_Fast_GET_ITEM( start_obj, i ) );
        else
            a[i] = 0.0f;
    }

    Py_DECREF( start_obj );

    end_obj = PySequence_Fast( end_obj, "Expected a tuple or list for end." );

    if( !end_obj )
        return -1;

    a = &self->end.min.x;

    for( Py_ssize_t i = 0; i < 4; i++ ) {
        if( i < PySequence_Fast_GET_SIZE( end_obj ) )
            a[i] = PyFloat_AsDouble( PySequence_Fast_GET_ITEM( end_obj, i ) );
        else
            a[i] = 0.0f;
    }

    Py_DECREF( end_obj );

    return 0;
}

static void
LerpFunc_getValues( py_obj_LerpFunc *self, int count, double *frames, double (*outValues)[4] ) {
    for( int i = 0; i < count; i++ ) {
        outValues[i][0] = frames[i] * (self->end.min.x - self->start.min.x) / self->length + self->start.min.x;
        outValues[i][1] = frames[i] * (self->end.min.y - self->start.min.y) / self->length + self->start.min.y;
        outValues[i][2] = frames[i] * (self->end.max.x - self->start.max.x) / self->length + self->start.max.x;
        outValues[i][3] = frames[i] * (self->end.max.y - self->start.max.y) / self->length + self->start.max.y;
    }
}

static FrameFunctionFuncs LerpFunc_sourceFuncs = {
    0,
    .get_values = (framefunc_get_values_func) LerpFunc_getValues,
};

DECLARE_GETTER(LerpFunc)
DECLARE_DEFAULT_GETSETTERS(LerpFunc)
DECLARE_FRAMEFUNC(LerpFunc, generic_dealloc)


EXPORT bool
py_framefunc_take_source( PyObject *source, FrameFunctionHolder *holder ) {
    Py_CLEAR( holder->source );
    Py_CLEAR( holder->csource );

    memset( holder, 0, sizeof(FrameFunctionHolder) );

    if( source == NULL || source == Py_None )
        return true;

    box2i const_box2i;

    if( py_parse_box2i( source, &const_box2i ) ) {
        holder->constant[0] = const_box2i.min.x;
        holder->constant[1] = const_box2i.min.y;
        holder->constant[2] = const_box2i.max.x;
        holder->constant[3] = const_box2i.max.y;

        return true;
    }
    else {
        PyErr_Clear();
    }

    box2f const_box2f;

    if( py_parse_box2f( source, &const_box2f ) ) {
        holder->constant[0] = const_box2f.min.x;
        holder->constant[1] = const_box2f.min.y;
        holder->constant[2] = const_box2f.max.x;
        holder->constant[3] = const_box2f.max.y;

        return true;
    }
    else {
        PyErr_Clear();
    }

    if( PyTuple_Check( source ) ) {
        Py_ssize_t length = PyTuple_GET_SIZE( source );

        if( !length ) {
            PyErr_SetString( PyExc_ValueError, "An empty tuple was passed." );
            return false;
        }
        else if( length > 4 ) {
            PyErr_Format( PyExc_ValueError, "One of the tuples passed has more than four entries (%" PRIdPTR ").", (intptr_t) length );
            return false;
        }

        for( Py_ssize_t i = 0; i < length; i++ ) {
            PyObject *f = PyNumber_Float( PyTuple_GET_ITEM( source, i ) );

            if( !f )
                return false;

            holder->constant[i] = PyFloat_AS_DOUBLE( f );
            Py_CLEAR( f );

            if( PyErr_Occurred() )
                return false;
        }

        return true;
    }
    else {
        // Try to interpret it as a number
        PyObject *asFloat = PyNumber_Float( source );

        if( asFloat ) {
            holder->constant[0] = PyFloat_AS_DOUBLE( asFloat );
            Py_CLEAR( asFloat );

            return true;
        }
        else {
            // Clear any errors
            PyErr_Clear();
        }
    }

    Py_INCREF( source );
    holder->source = source;
    holder->csource = PyObject_GetAttrString( source, FRAME_FUNCTION_FUNCS );

    if( !PyCapsule_IsValid( holder->csource, FRAME_FUNCTION_FUNCS ) ) {
        Py_CLEAR( holder->source );
        PyErr_SetString( PyExc_Exception, "The source didn't have an acceptable " FRAME_FUNCTION_FUNCS " attribute." );
        return false;
    }

    holder->funcs = (FrameFunctionFuncs*) PyCapsule_GetPointer(
        holder->csource, FRAME_FUNCTION_FUNCS );

    return true;
}

EXPORT int
framefunc_get_i32( FrameFunctionHolder *holder, double frame ) {
    if( holder->funcs && holder->funcs->get_values ) {
        double dresult[4];

        holder->funcs->get_values( holder->source, 1, &frame, &dresult );

        return lround( dresult[0] );
    }
    else {
        return lround( holder->constant[0] );
    }
}

EXPORT float
framefunc_get_f32( FrameFunctionHolder *holder, double frame ) {
    if( holder->funcs && holder->funcs->get_values ) {
        double dresult[4];

        holder->funcs->get_values( holder->source, 1, &frame, &dresult );

        return (float) dresult[0];
    }
    else {
        return (float) holder->constant[0];
    }
}

EXPORT void
framefunc_get_v2f( v2f *result, FrameFunctionHolder *holder, double frame ) {
    if( holder->funcs && holder->funcs->get_values ) {
        double dresult[4];

        holder->funcs->get_values( holder->source, 1, &frame, &dresult );

        *result = (v2f) { (float) dresult[0], (float) dresult[1] };
    }
    else {
        *result = (v2f) { (float) holder->constant[0], (float) holder->constant[1] };
    }
}

EXPORT void
framefunc_get_box2i( box2i *result, FrameFunctionHolder *holder, double frame ) {
    if( holder->funcs && holder->funcs->get_values ) {
        double dresult[4];

        holder->funcs->get_values( holder->source, 1, &frame, &dresult );

        *result = (box2i) { { lround( dresult[0] ), lround( dresult[1] ) },
            { lround( dresult[2] ), lround( dresult[3] ) } };
    }
    else {
        *result = (box2i) { { lround( holder->constant[0] ), lround( holder->constant[1] ) },
            { lround( holder->constant[2] ), lround( holder->constant[3] ) } };
    }
}

EXPORT void
framefunc_get_rgba_f32( rgba_f32 *result, FrameFunctionHolder *holder, double frame ) {
    if( holder->funcs && holder->funcs->get_values ) {
        double dresult[4];

        holder->funcs->get_values( holder->source, 1, &frame, &dresult );

        *result = (rgba_f32) { (float) dresult[0], (float) dresult[1],
            (float) dresult[2], clampf( (float) dresult[3], 0.0f, 1.0f ) };
    }
    else {
        *result = (rgba_f32) { (float) holder->constant[0], (float) holder->constant[1],
            (float) holder->constant[2], clampf( (float) holder->constant[3], 0.0f, 1.0f ) };
    }
}

static PyObject *
py_frame_func_get( PyObject *self, PyObject *args, PyObject *kw ) {
    PyObject *frames_obj;

    if( !PyArg_ParseTuple( args, "O", &frames_obj ) )
        return NULL;

    // For now, we'll be very specific about what we expect out of frames
    double *frames;
    ssize_t count;

    if( PySequence_Check( frames_obj ) ) {
        PyObject *fast = PySequence_Fast( frames_obj, "What? They told me it was a sequence!" );
        count = PySequence_Fast_GET_SIZE( fast );

        frames = g_malloc( sizeof(double) * count );

        for( ssize_t i = 0; i < count; i++ ) {
            frames[i] = PyFloat_AsDouble( PySequence_Fast_GET_ITEM( fast, i ) );

            if( PyErr_Occurred() ) {
                g_free( frames );
                return NULL;
            }
        }

        Py_DECREF(fast);
    }
    else {
        frames = g_malloc( sizeof(double) );
        count = 1;

        *frames = PyFloat_AsDouble( frames_obj );

        if( PyErr_Occurred() ) {
            g_free( frames );
            return NULL;
        }
    }

    FrameFunctionHolder holder = { .source = NULL };

    if( !py_framefunc_take_source( self, &holder ) ) {
        g_free( frames );
        return NULL;
    }

    PyObject *result_obj = PyList_New( count );

    if( !holder.funcs ) {
        // Take the constant
        PyObject *const_obj = Py_BuildValue( "dddd", holder.constant[0],
            holder.constant[1], holder.constant[2], holder.constant[3] );

        for( ssize_t i = 0; i < count; i++ ) {
            Py_INCREF(const_obj);
            PyList_SET_ITEM( result_obj, i, const_obj );
        }

        Py_DECREF(const_obj);   // The reference returned by Py_BuildValue
    }
    else if( holder.funcs->get_values ) {
        double (*result)[4] = g_malloc( sizeof(double) * count * 4 );
        holder.funcs->get_values( holder.source, count, frames, result );

        for( ssize_t i = 0; i < count; i++ )
            PyList_SET_ITEM( result_obj, i, Py_BuildValue( "dddd", result[i][0],
                result[i][1], result[i][2], result[i][3] ) );

        g_free( result );
    }

    py_framefunc_take_source( NULL, &holder );
    g_free( frames );

    return result_obj;
}

static PyMethodDef FrameFunction_methods[] = {
    { "get_values", (PyCFunction) py_frame_func_get, METH_VARARGS,
        "Get a list of values from a frame function.\n"
        "\n"
        "value_list = source.get_values(frames)\n"
        "\n"
        "source: A frame function.\n"
        "frames: An float or a list of floats of the frames to get values for." },
    { NULL }
};

EXPORT PyTypeObject py_type_FrameFunction = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.FrameFunction",
    .tp_basicsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_methods = FrameFunction_methods,
};


void init_basicframefuncs( PyObject *module ) {
    SETUP_FRAMEFUNC(LinearFrameFunc);
    SETUP_FRAMEFUNC(LerpFunc);
}



