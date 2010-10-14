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
        PyObject_HEAD_INIT(NULL) \
        0, \
        "fluggo.media.process." #name, \
        sizeof(py_obj_##name), \
        .tp_flags = Py_TPFLAGS_DEFAULT, \
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
    name##_pysourceFuncs = PyCObject_FromVoidPtr( &name##_sourceFuncs, NULL );


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
LinearFrameFunc_getValues_f32( py_obj_LinearFrameFunc *self, int count, int64_t *frames, int64_t div, float *outValues ) {
    for( int i = 0; i < count; i++ )
        outValues[i] = (frames[i] / (float) div) * self->a + self->b;
}

static FrameFunctionFuncs LinearFrameFunc_sourceFuncs = {
    0,
    .get_values_f32 = (framefunc_get_values_f32_func) LinearFrameFunc_getValues_f32
};

DECLARE_GETTER(LinearFrameFunc)
DECLARE_DEFAULT_GETSETTERS(LinearFrameFunc)
DECLARE_FRAMEFUNC(LinearFrameFunc, generic_dealloc)


typedef struct {
    PyObject_HEAD
    box2f start, end;
    int64_t length;
} py_obj_LerpFunc;

static int
LerpFunc_init( py_obj_LerpFunc *self, PyObject *args, PyObject *kwds ) {
    static char *kwlist[] = { "start", "end", "length", NULL };
    PyObject *start_obj, *end_obj;

    if( !PyArg_ParseTupleAndKeywords( args, kwds, "OOL", kwlist, &start_obj, &end_obj, &self->length ) )
        return -1;

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
LerpFunc_getValues_f32( py_obj_LerpFunc *self, int count, int64_t *frames, int64_t div, float *outValues ) {
    for( int i = 0; i < count; i++ ) {
        outValues[i] = (frames[i] / (float)(div * self->length)) * (self->end.min.x - self->start.min.x) + self->start.min.x;
    }
}

static void
LerpFunc_getValues_v2f( py_obj_LerpFunc *self, int count, int64_t *frames, int64_t div, v2f *outValues ) {
    for( int i = 0; i < count; i++ ) {
        outValues[i].x = (frames[i] / (float)(div * self->length)) * (self->end.min.x - self->start.min.x) + self->start.min.x;
        outValues[i].y = (frames[i] / (float)(div * self->length)) * (self->end.min.y - self->start.min.y) + self->start.min.y;
    }
}

static void
LerpFunc_getValues_box2f( py_obj_LerpFunc *self, int count, int64_t *frames, int64_t div, box2f *outValues ) {
    for( int i = 0; i < count; i++ ) {
        outValues[i].min.x = (frames[i] / (float)(div * self->length)) * (self->end.min.x - self->start.min.x) + self->start.min.x;
        outValues[i].min.y = (frames[i] / (float)(div * self->length)) * (self->end.min.y - self->start.min.y) + self->start.min.y;
        outValues[i].max.x = (frames[i] / (float)(div * self->length)) * (self->end.max.x - self->start.max.x) + self->start.max.x;
        outValues[i].max.y = (frames[i] / (float)(div * self->length)) * (self->end.max.y - self->start.max.y) + self->start.max.y;
    }
}

static FrameFunctionFuncs LerpFunc_sourceFuncs = {
    0,
    .get_values_f32 = (framefunc_get_values_f32_func) LerpFunc_getValues_f32,
    .get_values_v2f = (framefunc_get_values_v2f_func) LerpFunc_getValues_v2f,
    .get_values_box2f = (framefunc_get_values_box2f_func) LerpFunc_getValues_box2f
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

    if( py_parse_box2i( source, &holder->constant.const_box2i ) ) {
        holder->constant_type = CONST_TYPE_INT32;
        return true;
    }
    else {
        PyErr_Clear();
    }

    if( py_parse_box2f( source, &holder->constant.const_box2f ) ) {
        holder->constant_type = CONST_TYPE_FLOAT32;
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

        // Check to see if they all support the index interface
        bool is_index = true;

        for( Py_ssize_t i = 0; i < length; i++ ) {
            if( !PyIndex_Check( PyTuple_GET_ITEM( source, i ) ) )
                is_index = false;
        }

        if( is_index ) {
            // A tuple of integers or integer-alikes
            for( Py_ssize_t i = 0; i < length; i++ ) {
                PyObject *index = PyNumber_Index( PyTuple_GET_ITEM( source, i ) );

                if( index == NULL )
                    return false;

                holder->constant.const_i32_array[i] = (int) PyInt_AsLong( index );
                Py_CLEAR( index );

                if( holder->constant.const_i32_array[i] == -1 && PyErr_Occurred() )
                    return false;
            }

            holder->constant_type = CONST_TYPE_INT32;
        }
        else {
            // A tuple of floats?
            for( Py_ssize_t i = 0; i < length; i++ ) {
                PyObject *f32 = PyNumber_Float( PyTuple_GET_ITEM( source, i ) );

                if( f32 == NULL )
                    return false;

                holder->constant.const_f32_array[i] = (float) PyFloat_AsDouble( f32 );
                Py_CLEAR( f32 );

                if( PyErr_Occurred() )
                    return false;
            }

            holder->constant_type = CONST_TYPE_FLOAT32;
        }

        return true;
    }
    else {
        // Try to interpret it as a number

        if( PyIndex_Check( source ) ) {
            PyObject *index = PyNumber_Index( source );

            if( index == NULL )
                return false;

            holder->constant.const_i32 = (int) PyInt_AsLong( index );
            Py_CLEAR( index );

            if( holder->constant.const_i32 == -1 && PyErr_Occurred() )
                return false;

            holder->constant_type = CONST_TYPE_INT32;
            return true;
        }
        else {
            PyObject *asFloat = PyNumber_Float( source );

            if( asFloat ) {
                holder->constant.const_f32 = (float) PyFloat_AS_DOUBLE( asFloat );
                Py_CLEAR( asFloat );

                holder->constant_type = CONST_TYPE_FLOAT32;
                return true;
            }
            else {
                // Clear any errors
                PyErr_Clear();
            }
        }
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

EXPORT int
frameFunc_get_i32( FrameFunctionHolder *holder, int64_t frame, int64_t div ) {
    int result = 0;

    if( holder->constant_type == CONST_TYPE_INT32 )
        result = holder->constant.const_i32;

    if( holder->funcs && holder->funcs->get_values_i32 )
        holder->funcs->get_values_i32( holder->source, 1, &frame, div, &result );

    return result;
}

EXPORT float
frameFunc_get_f32( FrameFunctionHolder *holder, int64_t frame, int64_t div ) {
    float result = 0;

    if( holder->constant_type == CONST_TYPE_FLOAT32 )
        result = holder->constant.const_f32;

    if( holder->funcs && holder->funcs->get_values_f32 )
        holder->funcs->get_values_f32( holder->source, 1, &frame, div, &result );

    return result;
}

EXPORT void
frameFunc_get_v2f( FrameFunctionHolder *holder, int64_t frame, int64_t div, v2f *result ) {
    *result = (v2f) { 0.0f, 0.0f };

    if( holder->constant_type == CONST_TYPE_FLOAT32 ) {
        *result = holder->constant.const_v2f;
    }
    else if( holder->constant_type == CONST_TYPE_INT32 ) {
        result->x = holder->constant.const_v2i.x;
        result->y = holder->constant.const_v2i.y;
    }

    if( holder->funcs && holder->funcs->get_values_v2f )
        holder->funcs->get_values_v2f( holder->source, 1, &frame, div, result );
}

EXPORT void
frameFunc_get_box2i( FrameFunctionHolder *holder, int64_t frame, int64_t div, box2i *result ) {
    *result = (box2i) { { 0 } };

    if( holder->constant_type == CONST_TYPE_INT32 ) {
        *result = holder->constant.const_box2i;
    }
    else if( holder->constant_type == CONST_TYPE_FLOAT32 ) {
        result->min.x = lroundf( holder->constant.const_box2f.min.x );
        result->min.y = lroundf( holder->constant.const_box2f.min.y );
        result->max.x = lroundf( holder->constant.const_box2f.max.x );
        result->max.y = lroundf( holder->constant.const_box2f.max.y );
    }

    if( holder->funcs && holder->funcs->get_values_box2i )
        holder->funcs->get_values_box2i( holder->source, 1, &frame, div, result );
}

PyObject *
py_frame_func_get( PyObject *self, PyObject *args, PyObject *kw ) {
    static char *kwlist[] = { "source", "frames", "div", NULL };
    PyObject *source_obj, *frames_obj;
    int64_t div = INT64_C(1);

    if( !PyArg_ParseTupleAndKeywords( args, kw, "OO|I", kwlist,
            &source_obj, &frames_obj, &div ) )
        return NULL;

    // For now, we'll be very specific about what we expect out of frames
    int64_t *frames;
    ssize_t count;

    if( PySequence_Check( frames_obj ) ) {
        PyObject *fast = PySequence_Fast( frames_obj, "What? They told me it was a sequence!" );
        count = PySequence_Fast_GET_SIZE( fast );

        frames = g_malloc( sizeof(int64_t) * count );

        for( ssize_t i = 0; i < count; i++ ) {
            frames[i] = PyLong_AsLongLong( PySequence_Fast_GET_ITEM( fast, i ) );

            if( frames[i] == -1 && PyErr_Occurred() )
                return NULL;
        }

        Py_DECREF(fast);
    }
    else {
        frames = g_malloc( sizeof(int64_t) );
        count = 1;

        *frames = PyLong_AsLongLong( frames_obj );

        if( *frames == -1 && PyErr_Occurred() ) {
            g_free( frames );
            return NULL;
        }
    }

    FrameFunctionHolder holder = { .source = NULL };

    if( !py_framefunc_take_source( source_obj, &holder ) ) {
        g_free( frames );
        return NULL;
    }

    // Aim for the highest type supported by this interface
    // box2f, box2i, v2f, v2i, f32, i32
    PyObject *result_obj = PyList_New( count );

    if( !holder.funcs ) {
        // Take the constants
        if( holder.constant_type == CONST_TYPE_INT32 ) {
            PyObject *const_obj = py_make_box2i( &holder.constant.const_box2i );

            for( ssize_t i = 0; i < count; i++ ) {
                Py_INCREF(const_obj);
                PyList_SET_ITEM( result_obj, i, const_obj );
            }

            Py_DECREF(const_obj);   // The reference returned by py_make_box2i
        }
        else if( holder.constant_type == CONST_TYPE_FLOAT32 ) {
            PyObject *const_obj = py_make_box2f( &holder.constant.const_box2f );

            for( ssize_t i = 0; i < count; i++ ) {
                Py_INCREF(const_obj);
                PyList_SET_ITEM( result_obj, i, const_obj );
            }

            Py_DECREF(const_obj);   // The reference returned by py_make_box2f
        }
        else {
            Py_CLEAR(result_obj);
            result_obj = Py_None;
            Py_INCREF(result_obj);
        }
    }
    else {
        if( holder.funcs->get_values_box2f ) {
            box2f *result = g_malloc( sizeof(box2f) * count );
            holder.funcs->get_values_box2f( holder.source, count, frames, div, result );

            for( ssize_t i = 0; i < count; i++ )
                PyList_SET_ITEM( result_obj, i, py_make_box2f( &result[i] ) );

            g_free( result );
        }
        else if( holder.funcs->get_values_box2i ) {
            box2i *result = g_malloc( sizeof(box2i) * count );
            holder.funcs->get_values_box2i( holder.source, count, frames, div, result );

            for( ssize_t i = 0; i < count; i++ )
                PyList_SET_ITEM( result_obj, i, py_make_box2i( &result[i] ) );

            g_free( result );
        }
        else if( holder.funcs->get_values_v2f ) {
            v2f *result = g_malloc( sizeof(v2f) * count );
            holder.funcs->get_values_v2f( holder.source, count, frames, div, result );

            for( ssize_t i = 0; i < count; i++ )
                PyList_SET_ITEM( result_obj, i, py_make_v2f( &result[i] ) );

            g_free( result );
        }
        else if( holder.funcs->get_values_v2i ) {
            v2i *result = g_malloc( sizeof(v2i) * count );
            holder.funcs->get_values_v2i( holder.source, count, frames, div, result );

            for( ssize_t i = 0; i < count; i++ )
                PyList_SET_ITEM( result_obj, i, py_make_v2i( &result[i] ) );

            g_free( result );
        }
        else if( holder.funcs->get_values_f32 ) {
            float *result = g_malloc( sizeof(float) * count );
            holder.funcs->get_values_f32( holder.source, count, frames, div, result );

            for( ssize_t i = 0; i < count; i++ )
                PyList_SET_ITEM( result_obj, i, PyFloat_FromDouble( result[i] ) );

            g_free( result );
        }
        else if( holder.funcs->get_values_i32 ) {
            int *result = g_malloc( sizeof(int) * count );
            holder.funcs->get_values_i32( holder.source, count, frames, div, result );

            for( ssize_t i = 0; i < count; i++ )
                PyList_SET_ITEM( result_obj, i, PyInt_FromLong( result[i] ) );

            g_free( result );
        }
    }

    py_framefunc_take_source( NULL, &holder );
    g_free( frames );

    return result_obj;
}


void init_basicframefuncs( PyObject *module ) {
    SETUP_FRAMEFUNC(LinearFrameFunc);
    SETUP_FRAMEFUNC(LerpFunc);
}



