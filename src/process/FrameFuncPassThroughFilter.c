/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2009-12 Brian J. Crowell <brian@fluggo.com>

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

static PyObject *pysourceFuncs;

typedef struct {
    PyObject_HEAD

    FrameFunctionHolder source;
    double offset;
    GStaticRWLock rwlock;
} py_obj_FrameFuncPassThroughFilter;

static int
FrameFuncPassThroughFilter_init( py_obj_FrameFuncPassThroughFilter *self, PyObject *args, PyObject *kwds ) {
    PyObject *source;
    self->offset = 0.0;
    self->source.source = NULL;
    self->source.csource = NULL;
    self->source.constant[0] = 0.0;
    self->source.constant[1] = 0.0;
    self->source.constant[2] = 0.0;
    self->source.constant[3] = 0.0;

    static char *kwlist[] = { "source", "offset", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kwds, "O|d", kwlist,
            &source, &self->offset ) )
        return -1;

    if( !py_framefunc_take_source( source, &self->source ) )
        return -1;

    g_static_rw_lock_init( &self->rwlock );

    return 0;
}

static void
FrameFuncPassThroughFilter_get_values( py_obj_FrameFuncPassThroughFilter *self, ssize_t count, double *frames, double (*out_values)[4] ) {
    g_static_rw_lock_reader_lock( &self->rwlock );

    if( self->source.funcs && self->source.funcs->get_values ) {
        // Add the offset
        double *frames_temp = NULL;

        if( self->offset != 0.0 ) {
            frames_temp = g_new( double, count );

            for( ssize_t i = 0; i < count; i++ )
                frames_temp[i] = frames[i] + self->offset;
        }

        self->source.funcs->get_values( self->source.source, count, frames_temp ? frames_temp : frames, out_values );

        g_free( frames_temp );
    }
    else {
        // Use the constants
        for( ssize_t i = 0; i < count; i++ ) {
            out_values[i][0] = self->source.constant[0];
            out_values[i][1] = self->source.constant[1];
            out_values[i][2] = self->source.constant[2];
            out_values[i][3] = self->source.constant[3];
        }
    }

    g_static_rw_lock_reader_unlock( &self->rwlock );
}

static void
FrameFuncPassThroughFilter_dealloc( py_obj_FrameFuncPassThroughFilter *self ) {
    py_framefunc_take_source( NULL, &self->source );
    g_static_rw_lock_free( &self->rwlock );

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static PyObject *
FrameFuncPassThroughFilter_getSource( py_obj_FrameFuncPassThroughFilter *self ) {
    if( self->source.source == NULL )
        Py_RETURN_NONE;

    Py_INCREF((PyObject *) self->source.source);
    return (PyObject *) self->source.source;
}

static PyObject *
FrameFuncPassThroughFilter_setSource( py_obj_FrameFuncPassThroughFilter *self, PyObject *args, void *closure ) {
    PyObject *source;

    if( !PyArg_ParseTuple( args, "O", &source ) )
        return NULL;

    g_static_rw_lock_writer_lock( &self->rwlock );

    if( !py_framefunc_take_source( source, &self->source ) ) {
        g_static_rw_lock_writer_unlock( &self->rwlock );
        return NULL;
    }

    g_static_rw_lock_writer_unlock( &self->rwlock );

    Py_RETURN_NONE;
}

static PyObject *
FrameFuncPassThroughFilter_get_offset( py_obj_FrameFuncPassThroughFilter *self, void *closure ) {
    return PyFloat_FromDouble( self->offset );
}

static int
FrameFuncPassThroughFilter_set_offset( py_obj_FrameFuncPassThroughFilter *self, PyObject *value, void *closure ) {
    double offset = PyFloat_AsDouble( value );

    if( offset == -1.0 && PyErr_Occurred() )
        return -1;

    self->offset = offset;
    return 0;
}

static FrameFunctionFuncs source_funcs = {
    .get_values = (framefunc_get_values_func) FrameFuncPassThroughFilter_get_values,
};

static PyObject *
FrameFuncPassThroughFilter_get_funcs( py_obj_FrameFuncPassThroughFilter *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef FrameFuncPassThroughFilter_getsetters[] = {
    { FRAME_FUNCTION_FUNCS, (getter) FrameFuncPassThroughFilter_get_funcs, NULL, "Frame function C API." },
    { "offset", (getter) FrameFuncPassThroughFilter_get_offset, (setter) FrameFuncPassThroughFilter_set_offset, "Get or set the offset." },
    { NULL }
};

static PyMethodDef FrameFuncPassThroughFilter_methods[] = {
    { "source", (PyCFunction) FrameFuncPassThroughFilter_getSource, METH_NOARGS,
        "Gets the source frame function." },
    { "set_source", (PyCFunction) FrameFuncPassThroughFilter_setSource, METH_VARARGS,
        "Sets the source frame function." },
    { NULL }
};

static PyTypeObject py_type_FrameFuncPassThroughFilter = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.FrameFuncPassThroughFilter",
    .tp_basicsize = sizeof(py_obj_FrameFuncPassThroughFilter),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base = &py_type_FrameFunction,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) FrameFuncPassThroughFilter_dealloc,
    .tp_init = (initproc) FrameFuncPassThroughFilter_init,
    .tp_getset = FrameFuncPassThroughFilter_getsetters,
    .tp_methods = FrameFuncPassThroughFilter_methods,
};

void init_FrameFuncPassThroughFilter( PyObject *module ) {
    if( PyType_Ready( &py_type_FrameFuncPassThroughFilter ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_FrameFuncPassThroughFilter );
    PyModule_AddObject( module, "FrameFuncPassThroughFilter", (PyObject *) &py_type_FrameFuncPassThroughFilter );

    pysourceFuncs = PyCapsule_New( &source_funcs, FRAME_FUNCTION_FUNCS, NULL );
}



