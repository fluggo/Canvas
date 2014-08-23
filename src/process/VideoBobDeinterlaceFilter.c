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

    video_source *source;
    GRWLock rwlock;
} py_obj_VideoBobDeinterlaceFilter;

static int
VideoBobDeinterlaceFilter_init( py_obj_VideoBobDeinterlaceFilter *self, PyObject *args, PyObject *kwds ) {
    PyObject *source;

    static char *kwlist[] = { "source", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kwds, "O", kwlist,
            &source ) )
        return -1;

    if( !py_video_take_source( source, &self->source ) )
        return -1;

    g_rw_lock_init( &self->rwlock );

    return 0;
}

static void
VideoBobDeinterlaceFilter_get_frame_gl( py_obj_VideoBobDeinterlaceFilter *self, int frame_index, rgba_frame_gl *frame ) {
    rgba_frame_gl temp = { .full_window = frame->full_window };
    int source_index = frame_index / 2;
    bool upper_field = (frame_index & 1) == 0;

    g_rw_lock_reader_lock( &self->rwlock );

    video_get_frame_gl( self->source, source_index, &temp );

    g_rw_lock_reader_unlock( &self->rwlock );

    video_deinterlace_bob_gl( frame, &temp, upper_field );

    glDeleteTextures( 1, &temp.texture );
}

static void
VideoBobDeinterlaceFilter_dealloc( py_obj_VideoBobDeinterlaceFilter *self ) {
    py_video_take_source( NULL, &self->source );
    g_rw_lock_clear( &self->rwlock );

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static PyObject *
VideoBobDeinterlaceFilter_get_source( py_obj_VideoBobDeinterlaceFilter *self, void *closure ) {
    if( self->source == NULL )
        Py_RETURN_NONE;

    Py_INCREF((PyObject *) self->source->obj);
    return (PyObject *) self->source->obj;
}

static PyObject *
VideoBobDeinterlaceFilter_set_source( py_obj_VideoBobDeinterlaceFilter *self, PyObject *args, void *closure ) {
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
    .get_frame_gl = (video_get_frame_gl_func) VideoBobDeinterlaceFilter_get_frame_gl
};

static PyObject *
VideoBobDeinterlaceFilter_get_funcs( py_obj_VideoBobDeinterlaceFilter *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef VideoBobDeinterlaceFilter_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) VideoBobDeinterlaceFilter_get_funcs, NULL, "Video frame source C API." },
    { "source", (getter) VideoBobDeinterlaceFilter_get_source, NULL,
        "The video source." },
    { NULL }
};

static PyMethodDef VideoBobDeinterlaceFilter_methods[] = {
    { "set_source", (PyCFunction) VideoBobDeinterlaceFilter_set_source, METH_VARARGS,
        "Sets the video source." },
    { NULL }
};

static PyTypeObject py_type_VideoBobDeinterlaceFilter = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.VideoBobDeinterlaceFilter",
    .tp_basicsize = sizeof(py_obj_VideoBobDeinterlaceFilter),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base = &py_type_VideoSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) VideoBobDeinterlaceFilter_dealloc,
    .tp_init = (initproc) VideoBobDeinterlaceFilter_init,
    .tp_getset = VideoBobDeinterlaceFilter_getsetters,
    .tp_methods = VideoBobDeinterlaceFilter_methods,
};

void init_VideoBobDeinterlaceFilter( PyObject *module ) {
    if( PyType_Ready( &py_type_VideoBobDeinterlaceFilter ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_VideoBobDeinterlaceFilter );
    PyModule_AddObject( module, "VideoBobDeinterlaceFilter", (PyObject *) &py_type_VideoBobDeinterlaceFilter );

    pysourceFuncs = PyCapsule_New( &source_funcs, VIDEO_FRAME_SOURCE_FUNCS, NULL );
}



