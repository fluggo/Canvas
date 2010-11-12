/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2010 Brian J. Crowell <brian@fluggo.com>

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

#define POINT_HOLD      0
#define POINT_LINEAR    1
#define POINT_MAX       1

typedef struct {
    PyObject_HEAD

    GSequence *sequence;
    GStaticRWLock lock;
    GSequenceIter *iter;
} py_obj_AnimationFunc;

typedef struct {
    PyObject_HEAD
    py_obj_AnimationFunc *owner;
    GSequenceIter *iter;

    int type;
    double frame;
    double values[4];
} py_obj_AnimationPoint;

static gint
cmpx( py_obj_AnimationPoint *a, py_obj_AnimationPoint *b, gpointer user_data ) {
    if( a->frame == b->frame )
        return 0;

    if( a->frame > b->frame )
        return 1;

    return -1;
}

static bool
parse_value_obj( double result[4], PyObject *source ) {
    result[0] = 0.0;
    result[1] = 0.0;
    result[2] = 0.0;
    result[3] = 0.0;

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
            PyObject *f32 = PyNumber_Float( PyTuple_GET_ITEM( source, i ) );

            if( f32 == NULL )
                return false;

            result[i] = PyFloat_AS_DOUBLE( f32 );
            Py_CLEAR( f32 );

            if( PyErr_Occurred() )
                return false;
        }

        return true;
    }

    // Try to interpret it as a number
    PyObject *asFloat = PyNumber_Float( source );

    if( asFloat ) {
        result[0] = PyFloat_AS_DOUBLE( asFloat );
        Py_CLEAR( asFloat );

        return true;
    }

    return false;
}

static int
AnimationPoint_init( py_obj_AnimationPoint *self, PyObject *args, PyObject *kw ) {
    static char *kwlist[] = { "type", "frame", "value", NULL };
    PyObject *value_obj;

    if( !PyArg_ParseTupleAndKeywords( args, kw, "idO", kwlist,
            &self->type, &self->frame, &value_obj ) )
        return -1;

    if( self->type < 0 || self->type > POINT_MAX )
        PyErr_SetString( PyExc_Exception, "The given type value was invalid." );

    if( !parse_value_obj( self->values, value_obj ) )
        return -1;

    return 0;
}

static PyObject *
AnimationPoint_get_value( py_obj_AnimationPoint *self, void *closure ) {
    return Py_BuildValue( "dddd", self->values[0], self->values[1], self->values[2], self->values[3] );
}

static PyObject *
AnimationPoint_get_frame( py_obj_AnimationPoint *self, void *closure ) {
    return Py_BuildValue( "d", self->frame );
}

static int
AnimationPoint_set_frame( py_obj_AnimationPoint *self, PyObject *value, void *closure ) {
    double frame = PyFloat_AsDouble( value );

    if( PyErr_Occurred() )
        return -1;

    self->frame = frame;

    if( self->iter ) {
        g_static_rw_lock_writer_lock( &self->owner->lock );
        g_sequence_sort_changed( self->iter, (GCompareDataFunc) cmpx, NULL );
        g_static_rw_lock_writer_unlock( &self->owner->lock );
    }

    return 0;
}

static PyObject *
AnimationPoint_get_type( py_obj_AnimationPoint *self, void *closure ) {
    return Py_BuildValue( "i", self->type );
}

static PyGetSetDef AnimationPoint_getsetters[] = {
    { "value", (getter) AnimationPoint_get_value, NULL, "Value at this point in the animation." },
    { "frame", (getter) AnimationPoint_get_frame, (setter) AnimationPoint_set_frame, "Frame for this animation point, which may be fractional." },
    { "type", (getter) AnimationPoint_get_type, NULL, "Type of this point." },
    { NULL }
};

static int
AnimationPoint_traverse( py_obj_AnimationPoint *self, visitproc visit, void *arg ) {
    Py_VISIT( self->owner );
    return 0;
}

static int
AnimationPoint_clear( py_obj_AnimationPoint *self ) {
    Py_CLEAR( self->owner );
    return 0;
}

static void
AnimationPoint_dealloc( py_obj_AnimationPoint *self ) {
    Py_CLEAR( self->owner );
    self->ob_type->tp_free( (PyObject*) self );
}

static PyTypeObject py_type_AnimationPoint = {
    PyObject_HEAD_INIT(NULL)
    .ob_size = 0,
    .tp_name = "fluggo.media.process.AnimationPoint",
    .tp_basicsize = sizeof(py_obj_AnimationPoint),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AnimationPoint_dealloc,
    .tp_init = (initproc) AnimationPoint_init,
    .tp_traverse = (traverseproc) AnimationPoint_traverse,
    .tp_clear = (inquiry) AnimationPoint_clear,
    .tp_getset = AnimationPoint_getsetters,
};

static int
AnimationFunc_init( py_obj_AnimationFunc *self, PyObject *args, PyObject *kwds ) {
    self->sequence = g_sequence_new( NULL );
    g_static_rw_lock_init( &self->lock );
    self->iter = g_sequence_get_begin_iter( self->sequence );

    return 0;
}

static void
AnimationFunc_dealloc( py_obj_AnimationFunc *self ) {
    GSequenceIter *iter = g_sequence_get_begin_iter( self->sequence );

    while( !g_sequence_iter_is_end( iter ) ) {
        py_obj_AnimationPoint *item = (py_obj_AnimationPoint *) g_sequence_get( iter );

        item->iter = NULL;
        Py_CLEAR(item->owner);
        Py_CLEAR(item);

        iter = g_sequence_iter_next( iter );
    }

    g_sequence_free( self->sequence );
    g_static_rw_lock_free( &self->lock );

    self->ob_type->tp_free( (PyObject*) self );
}

static int
AnimationFunc_traverse( py_obj_AnimationFunc *self, visitproc visit, void *arg ) {
    g_static_rw_lock_reader_lock( &self->lock );

    GSequenceIter *iter = g_sequence_get_begin_iter( self->sequence );

    while( !g_sequence_iter_is_end( iter ) ) {
        PyObject *item = (PyObject *) g_sequence_get( iter );

        int result = visit( item, arg );

        if( result ) {
            g_static_rw_lock_reader_unlock( &self->lock );
            return result;
        }

        iter = g_sequence_iter_next( iter );
    }

    g_static_rw_lock_reader_unlock( &self->lock );
    return 0;
}

static PyObject *AnimationFunc_pysourceFuncs;

static PyObject *
AnimationFunc_get_funcs( PyObject *self, void *closure ) {
    Py_INCREF(AnimationFunc_pysourceFuncs);
    return AnimationFunc_pysourceFuncs;
};

static PyGetSetDef AnimationFunc_getsetters[] = {
    { FRAME_FUNCTION_FUNCS, (getter) AnimationFunc_get_funcs, NULL, "Frame function C API." },
    { NULL }
};

static Py_ssize_t
AnimationFunc_size( py_obj_AnimationFunc *self ) {
    g_static_rw_lock_reader_lock( &self->lock );
    Py_ssize_t result = g_sequence_get_length( self->sequence );
    g_static_rw_lock_reader_unlock( &self->lock );

    return result;
}

static PyObject *
AnimationFunc_get_item( py_obj_AnimationFunc *self, Py_ssize_t i ) {
    g_static_rw_lock_reader_lock( &self->lock );
    GSequenceIter *iter = g_sequence_get_iter_at_pos( self->sequence, i );

    if( g_sequence_iter_is_end( iter ) ) {
        PyErr_SetString( PyExc_IndexError, "Index was out of range." );
        return NULL;
    }

    PyObject *result = g_sequence_get( iter );
    g_static_rw_lock_reader_unlock( &self->lock );

    Py_INCREF( result );
    return result;
}

static PySequenceMethods AnimationFunc_sequence = {
    .sq_length = (lenfunc) AnimationFunc_size,
    .sq_item = (ssizeargfunc) AnimationFunc_get_item,
};

static py_obj_AnimationPoint *
AnimationFunc_add( py_obj_AnimationFunc *self, PyObject *args, PyObject *kw ) {
    py_obj_AnimationPoint *item;

    if( PyTuple_GET_SIZE( args ) == 3 ) {
        item = (py_obj_AnimationPoint*) PyObject_Call( (PyObject *) &py_type_AnimationPoint, args, kw );

        if( !item )
            return NULL;
    }
    else {
        if( !PyArg_ParseTuple( args, "O!", &py_type_AnimationPoint, &item ) )
            return NULL;

        if( item->owner != NULL ) {
            PyErr_SetString( PyExc_Exception, "This point already belongs to an animation." );
            return NULL;
        }

        Py_INCREF(item);
    }

    item->owner = self;
    Py_INCREF(item->owner);

    g_static_rw_lock_writer_lock( &self->lock );
    item->iter = g_sequence_insert_sorted( self->sequence, item, (GCompareDataFunc) cmpx, NULL );
    g_static_rw_lock_writer_unlock( &self->lock );

    Py_INCREF(item);
    return item;
}

static PyObject *
AnimationFunc_remove( py_obj_AnimationFunc *self, PyObject *args ) {
    py_obj_AnimationPoint *item;

    if( !PyArg_ParseTuple( args, "O!", &py_type_AnimationPoint, &item ) )
        return NULL;

    if( item->owner != self )
        Py_RETURN_NONE;

    g_static_rw_lock_writer_lock( &self->lock );
    g_sequence_remove( item->iter );
    g_static_rw_lock_writer_unlock( &self->lock );

    Py_CLEAR(item->owner);
    Py_CLEAR(item);

    Py_RETURN_NONE;
}

static PyMethodDef AnimationFunc_methods[] = {
    { "add", (PyCFunction) AnimationFunc_add, METH_VARARGS | METH_KEYWORDS,
        "Adds a new point to the animation." },
    { "remove", (PyCFunction) AnimationFunc_remove, METH_VARARGS,
        "Removes a point from the animation." },
    { NULL }
};

EXPORT PyTypeObject py_type_AnimationFunc = {
    PyObject_HEAD_INIT(NULL)
    .ob_size = 0,
    .tp_name = "fluggo.media.process.AnimationFunc",
    .tp_basicsize = sizeof(py_obj_AnimationFunc),
    .tp_base = &py_type_FrameFunction,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AnimationFunc_dealloc,
    .tp_init = (initproc) AnimationFunc_init,
    .tp_traverse = (traverseproc) AnimationFunc_traverse,
    .tp_getset = AnimationFunc_getsetters,
    .tp_methods = AnimationFunc_methods,
    .tp_as_sequence = &AnimationFunc_sequence,
};

static void
get_points( py_obj_AnimationFunc *self, double frame, py_obj_AnimationPoint **left, py_obj_AnimationPoint **right ) {
    // Put ourselves in a reasonable position to search
    if( g_sequence_iter_is_end( self->iter ) ) {
        if( g_sequence_iter_is_begin( self->iter ) ) {
            *left = NULL;
            *right = NULL;
            return;
        }

        self->iter = g_sequence_iter_prev( self->iter );
    }

    for( ;; ) {
        *left = (py_obj_AnimationPoint *) g_sequence_get( self->iter );

        if( frame < (*left)->frame ) {
            // Move backwards
            if( g_sequence_iter_is_begin( self->iter ) ) {
                *right = *left;
                *left = NULL;
                return;
            }

            self->iter = g_sequence_iter_prev( self->iter );
            continue;
        }

        GSequenceIter *next = g_sequence_iter_next( self->iter );

        if( g_sequence_iter_is_end( next ) ) {
            *right = NULL;
            return;
        }
        else {
            *right = g_sequence_get( next );

            if( frame >= (*right)->frame ) {
                self->iter = next;
                continue;
            }

            return;
        }
    }
}

static void
AnimationFunc_get_values( py_obj_AnimationFunc *self, int count, double *frames, double (*result)[4] ) {
    g_static_rw_lock_reader_lock( &self->lock );

    for( int i = 0; i < count; i++ ) {
        py_obj_AnimationPoint *left, *right;

        get_points( self, frames[i], &left, &right );

        if( !left ) {
            if( !right ) {
                result[i][0] = 0.0;
                result[i][1] = 0.0;
                result[i][2] = 0.0;
                result[i][3] = 0.0;
                continue;
            }

            // Take the right-hand values
            for( int j = 0; j < 4; j++ )
                result[i][j] = right->values[j];
        }
        else if( !right || left->type == POINT_HOLD ) {
            // Take the left-hand values
            for( int j = 0; j < 4; j++ )
                result[i][j] = left->values[j];
        }
        else if( left->type == POINT_LINEAR ) {
            // Lerp from left to right
            double distance = right->frame - left->frame;

            for( int j = 0; j < 4; j++ ) {
                result[i][j] = (right->values[j] * (frames[i] - left->frame) +
                    left->values[j] * (right->frame - frames[i])) / distance;
            }

            continue;
        }
        else {
            result[i][0] = 0.0;
            result[i][1] = 0.0;
            result[i][2] = 0.0;
            result[i][3] = 0.0;
        }
    }

    g_static_rw_lock_reader_unlock( &self->lock );
}

static FrameFunctionFuncs AnimationFunc_sourceFuncs = {
    0,
    .get_values = (framefunc_get_values_func) AnimationFunc_get_values,
};


void init_AnimationFunc( PyObject *module ) {
    if( PyType_Ready( &py_type_AnimationPoint ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_AnimationPoint );
    PyModule_AddObject( module, "AnimationPoint", (PyObject *) &py_type_AnimationPoint );

    if( PyType_Ready( &py_type_AnimationFunc ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_AnimationFunc );
    PyModule_AddObject( module, "AnimationFunc", (PyObject *) &py_type_AnimationFunc );

    PyModule_AddIntMacro( module, POINT_HOLD );
    PyModule_AddIntMacro( module, POINT_LINEAR );

    AnimationFunc_pysourceFuncs = PyCObject_FromVoidPtr( &AnimationFunc_sourceFuncs, NULL );
}

