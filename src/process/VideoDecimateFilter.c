/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2009-13 Brian J. Crowell <brian@fluggo.com>

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

    int divisor, offset;

    video_source *source;
    GRWLock rwlock;
} py_obj_VideoDecimateFilter;

static int
VideoDecimateFilter_init( py_obj_VideoDecimateFilter *self, PyObject *args, PyObject *kwds ) {
    PyObject *source;
    self->offset = 0;

    static char *kwlist[] = { "source", "divisor", "offset", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kwds, "Oi|i", kwlist,
            &source, &self->divisor, &self->offset ) )
        return -1;

    if( !py_video_take_source( source, &self->source ) )
        return -1;

    g_rw_lock_init( &self->rwlock );

    return 0;
}

static void
VideoDecimateFilter_get_frame_gl( py_obj_VideoDecimateFilter *self, int frame_index, rgba_frame_gl *frame ) {
    int source_index = frame_index;

    if( self->divisor == 0 )
        source_index = 0;
    else
        source_index *= self->divisor;

    source_index += self->offset;

    g_rw_lock_reader_lock( &self->rwlock );

    video_get_frame_gl( self->source, source_index, frame );

    g_rw_lock_reader_unlock( &self->rwlock );
}

static void
VideoDecimateFilter_dealloc( py_obj_VideoDecimateFilter *self ) {
    py_video_take_source( NULL, &self->source );
    g_rw_lock_clear( &self->rwlock );

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static PyObject *
VideoDecimateFilter_get_source( py_obj_VideoDecimateFilter *self, void *closure ) {
    if( self->source == NULL )
        Py_RETURN_NONE;

    Py_INCREF((PyObject *) self->source->obj);
    return (PyObject *) self->source->obj;
}

static PyObject *
VideoDecimateFilter_set_source( py_obj_VideoDecimateFilter *self, PyObject *args, void *closure ) {
    PyObject *source;

    if( !PyArg_ParseTuple( args, "O", &source ) )
        return NULL;

    g_rw_lock_writer_lock( &self->rwlock );

    if( !py_video_take_source( source, &self->source ) ) {
        g_rw_lock_writer_unlock( &self->rwlock );
        return NULL;
    }

    g_rw_lock_writer_unlock( &self->rwlock );

    Py_RETURN_NONE;
}

static video_frame_source_funcs source_funcs = {
    .get_frame_gl = (video_get_frame_gl_func) VideoDecimateFilter_get_frame_gl
};

static PyObject *
VideoDecimateFilter_get_funcs( py_obj_VideoDecimateFilter *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef VideoDecimateFilter_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) VideoDecimateFilter_get_funcs, NULL, "Video frame source C API." },
    { "source", (getter) VideoDecimateFilter_get_source, NULL,
        "The video source." },
    { NULL }
};

static PyMethodDef VideoDecimateFilter_methods[] = {
    { "set_source", (PyCFunction) VideoDecimateFilter_set_source, METH_VARARGS,
        "Sets the video source." },
    { NULL }
};

static PyTypeObject py_type_VideoDecimateFilter = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.VideoDecimateFilter",
    .tp_basicsize = sizeof(py_obj_VideoDecimateFilter),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base = &py_type_VideoSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) VideoDecimateFilter_dealloc,
    .tp_init = (initproc) VideoDecimateFilter_init,
    .tp_getset = VideoDecimateFilter_getsetters,
    .tp_methods = VideoDecimateFilter_methods,
};

void init_VideoDecimateFilter( PyObject *module ) {
    if( PyType_Ready( &py_type_VideoDecimateFilter ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_VideoDecimateFilter );
    PyModule_AddObject( module, "VideoDecimateFilter", (PyObject *) &py_type_VideoDecimateFilter );

    pysourceFuncs = PyCapsule_New( &source_funcs, VIDEO_FRAME_SOURCE_FUNCS, NULL );
}



