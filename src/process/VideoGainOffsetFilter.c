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

    video_source *source;
    FrameFunctionHolder gain_func, offset_func;
    GRWLock rwlock;
} py_obj_VideoGainOffsetFilter;

static int
VideoGainOffsetFilter_init( py_obj_VideoGainOffsetFilter *self, PyObject *args, PyObject *kwds ) {
    PyObject *source;
    framefunc_init( &self->gain_func, 1.0, 0.0, 0.0, 0.0 );
    framefunc_init( &self->offset_func, 0.0, 0.0, 0.0, 0.0 );

    PyObject *gain_obj = NULL, *offset_obj = NULL;

    static char *kwlist[] = { "source", "gain", "offset", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kwds, "O|OO", kwlist,
            &source, &gain_obj, &offset_obj ) )
        return -1;

    if( !py_video_take_source( source, &self->source ) )
        return -1;

    if( gain_obj && !py_framefunc_take_source( gain_obj, &self->gain_func ) ) {
        py_video_take_source( NULL, &self->source );
        return -1;
    }

    if( offset_obj && !py_framefunc_take_source( offset_obj, &self->offset_func ) ) {
        py_video_take_source( NULL, &self->source );
        py_framefunc_take_source( NULL, &self->gain_func );
        return -1;
    }

    g_rw_lock_init( &self->rwlock );

    return 0;
}

static void
VideoGainOffsetFilter_get_frame_gl( py_obj_VideoGainOffsetFilter *self, int frame_index, rgba_frame_gl *frame ) {
    rgba_frame_gl temp = { .full_window = frame->full_window };
    float gain, offset;

    g_rw_lock_reader_lock( &self->rwlock );

    video_get_frame_gl( self->source, frame_index, &temp );
    gain = framefunc_get_f32( &self->gain_func, frame_index );
    offset = framefunc_get_f32( &self->offset_func, frame_index );

    g_rw_lock_reader_unlock( &self->rwlock );

    video_filter_gain_offset_gl( frame, &temp, gain, offset );

    glDeleteTextures( 1, &temp.texture );
}

static void
VideoGainOffsetFilter_dealloc( py_obj_VideoGainOffsetFilter *self ) {
    py_video_take_source( NULL, &self->source );
    g_rw_lock_clear( &self->rwlock );

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static PyObject *
VideoGainOffsetFilter_get_source( py_obj_VideoGainOffsetFilter *self, void *closure ) {
    if( self->source == NULL )
        Py_RETURN_NONE;

    Py_INCREF((PyObject *) self->source->obj);
    return (PyObject *) self->source->obj;
}

static PyObject *
VideoGainOffsetFilter_set_source( py_obj_VideoGainOffsetFilter *self, PyObject *args, void *closure ) {
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

static PyObject *
VideoGainOffsetFilter_get_gain( py_obj_VideoGainOffsetFilter *self, void *closure ) {
    if( self->gain_func.source ) {
        Py_INCREF(self->gain_func.source);
        return self->gain_func.source;
    }

    return PyFloat_FromDouble( self->gain_func.constant[0] );
}

static int
VideoGainOffsetFilter_set_gain( py_obj_VideoGainOffsetFilter *self, PyObject *gain_obj, void *closure ) {
    g_rw_lock_writer_lock( &self->rwlock );
    bool success = py_framefunc_take_source( gain_obj, &self->gain_func );
    g_rw_lock_writer_unlock( &self->rwlock );

    if( success )
        return 0;
    else
        return -1;
}

static PyObject *
VideoGainOffsetFilter_get_offset( py_obj_VideoGainOffsetFilter *self, void *closure ) {
    if( self->offset_func.source ) {
        Py_INCREF(self->offset_func.source);
        return self->offset_func.source;
    }

    return PyFloat_FromDouble( self->offset_func.constant[0] );
}

static int
VideoGainOffsetFilter_set_offset( py_obj_VideoGainOffsetFilter *self, PyObject *offset_obj, void *closure ) {
    g_rw_lock_writer_lock( &self->rwlock );
    bool success = py_framefunc_take_source( offset_obj, &self->offset_func );
    g_rw_lock_writer_unlock( &self->rwlock );

    if( success )
        return 0;
    else
        return -1;
}

static video_frame_source_funcs source_funcs = {
    .get_frame_gl = (video_get_frame_gl_func) VideoGainOffsetFilter_get_frame_gl
};

static PyObject *
VideoGainOffsetFilter_get_funcs( py_obj_VideoGainOffsetFilter *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef VideoGainOffsetFilter_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) VideoGainOffsetFilter_get_funcs, NULL, "Video frame source C API." },
    { "gain", (getter) VideoGainOffsetFilter_get_gain, (setter) VideoGainOffsetFilter_set_gain, "The gain or its frame function." },
    { "offset", (getter) VideoGainOffsetFilter_get_offset, (setter) VideoGainOffsetFilter_set_offset, "The offset or its frame function." },
    { "source", (getter) VideoGainOffsetFilter_get_source, NULL,
        "The video source." },
    { NULL }
};

static PyMethodDef VideoGainOffsetFilter_methods[] = {
    { "set_source", (PyCFunction) VideoGainOffsetFilter_set_source, METH_VARARGS,
        "Sets the video source." },
    { NULL }
};

static PyTypeObject py_type_VideoGainOffsetFilter = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.VideoGainOffsetFilter",
    .tp_basicsize = sizeof(py_obj_VideoGainOffsetFilter),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base = &py_type_VideoSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) VideoGainOffsetFilter_dealloc,
    .tp_init = (initproc) VideoGainOffsetFilter_init,
    .tp_getset = VideoGainOffsetFilter_getsetters,
    .tp_methods = VideoGainOffsetFilter_methods,
};

void init_VideoGainOffsetFilter( PyObject *module ) {
    if( PyType_Ready( &py_type_VideoGainOffsetFilter ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_VideoGainOffsetFilter );
    PyModule_AddObject( module, "VideoGainOffsetFilter", (PyObject *) &py_type_VideoGainOffsetFilter );

    pysourceFuncs = PyCapsule_New( &source_funcs, VIDEO_FRAME_SOURCE_FUNCS, NULL );
}



