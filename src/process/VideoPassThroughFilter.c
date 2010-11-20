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

static PyObject *pysourceFuncs;

typedef struct {
    PyObject_HEAD

    video_source *source;
    int offset;
    GStaticRWLock rwlock;
} py_obj_VideoPassThroughFilter;

static int
VideoPassThroughFilter_init( py_obj_VideoPassThroughFilter *self, PyObject *args, PyObject *kwds ) {
    PyObject *source;
    self->offset = 0;

    if( !PyArg_ParseTuple( args, "O|i", &source, &self->offset ) )
        return -1;

    if( !py_video_take_source( source, &self->source ) )
        return -1;

    g_static_rw_lock_init( &self->rwlock );

    return 0;
}

static void
VideoPassThroughFilter_getFrame( py_obj_VideoPassThroughFilter *self, int frameIndex, rgba_frame_f16 *frame ) {
    g_static_rw_lock_reader_lock( &self->rwlock );
    video_get_frame_f16( self->source, frameIndex + self->offset, frame );
    g_static_rw_lock_reader_unlock( &self->rwlock );
}

static void
VideoPassThroughFilter_getFrame32( py_obj_VideoPassThroughFilter *self, int frameIndex, rgba_frame_f32 *frame ) {
    g_static_rw_lock_reader_lock( &self->rwlock );
    video_get_frame_f32( self->source, frameIndex + self->offset, frame );
    g_static_rw_lock_reader_unlock( &self->rwlock );
}

static void
VideoPassThroughFilter_getFrameGL( py_obj_VideoPassThroughFilter *self, int frameIndex, rgba_frame_gl *frame ) {
    g_static_rw_lock_reader_lock( &self->rwlock );
    video_get_frame_gl( self->source, frameIndex + self->offset, frame );
    g_static_rw_lock_reader_unlock( &self->rwlock );
}

static void
VideoPassThroughFilter_dealloc( py_obj_VideoPassThroughFilter *self ) {
    // BJC: This is the first time I'm writing r/w lock code for a filter, so
    // let me try to explain here why that is, and why there's no need to lock
    // in a dealloc.
    //
    // First, the problem: Without any kind of locking, it is possible to try
    // to call into the get_frame of an object that has been freed. It's a race
    // between one of the many threads that are trying to render and code that
    // calls py_video_take_source. If all filters cooperate with locking-- that
    // is, ensuring that each filter does not remove a reference to any video
    // source that it might be calling into-- the problem should be solved.
    //
    // There are two exceptions: init and dealloc. init is obvious-- nobody has
    // a reference to the object yet, so there can be no simultaneous get_frame
    // calls. dealloc is less obvious, because you might conceive of a situation
    // where an upstream has called get_frame, but then the Python thread
    // executes and deallocates the entire filter network. This is why it's very
    // important that EVERY filter and everything that calls get_frame
    // participates-- the top sink must make sure to finish any get_frame calls
    // before dealloc'ing sources. As long as it does that, no filter should
    // have to worry about syncing in dealloc.

    py_video_take_source( NULL, &self->source );
    g_static_rw_lock_free( &self->rwlock );

    self->ob_type->tp_free( (PyObject*) self );
}

static PyObject *
VideoPassThroughFilter_getSource( py_obj_VideoPassThroughFilter *self ) {
    if( self->source == NULL )
        Py_RETURN_NONE;

    Py_INCREF((PyObject *) self->source->obj);
    return (PyObject *) self->source->obj;
}

static PyObject *
VideoPassThroughFilter_setSource( py_obj_VideoPassThroughFilter *self, PyObject *args, void *closure ) {
    PyObject *source;

    if( !PyArg_ParseTuple( args, "O", &source ) )
        return NULL;

    g_static_rw_lock_writer_lock( &self->rwlock );

    if( !py_video_take_source( source, &self->source ) ) {
        g_static_rw_lock_writer_unlock( &self->rwlock );
        return NULL;
    }

    g_static_rw_lock_writer_unlock( &self->rwlock );

    Py_RETURN_NONE;
}

static PyObject *
VideoPassThroughFilter_get_offset( py_obj_VideoPassThroughFilter *self, void *closure ) {
    return PyInt_FromLong( self->offset );
}

static int
VideoPassThroughFilter_set_offset( py_obj_VideoPassThroughFilter *self, PyObject *value, void *closure ) {
    int offset = PyInt_AsLong( value );

    if( offset == -1 && PyErr_Occurred() )
        return -1;

    self->offset = offset;
    return 0;
}

static video_frame_source_funcs sourceFuncs = {
    .get_frame = (video_get_frame_func) VideoPassThroughFilter_getFrame,
    .get_frame_32 = (video_get_frame_32_func) VideoPassThroughFilter_getFrame32,
    .get_frame_gl = (video_get_frame_gl_func) VideoPassThroughFilter_getFrameGL
};

static PyObject *
VideoPassThroughFilter_getFuncs( py_obj_VideoPassThroughFilter *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef VideoPassThroughFilter_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) VideoPassThroughFilter_getFuncs, NULL, "Video frame source C API." },
    { "offset", (getter) VideoPassThroughFilter_get_offset, (setter) VideoPassThroughFilter_set_offset, "Get or set the offset." },
    { NULL }
};

static PyMethodDef VideoPassThroughFilter_methods[] = {
    { "source", (PyCFunction) VideoPassThroughFilter_getSource, METH_NOARGS,
        "Gets the video source." },
    { "set_source", (PyCFunction) VideoPassThroughFilter_setSource, METH_VARARGS,
        "Sets the video source." },
    { NULL }
};

static PyTypeObject py_type_VideoPassThroughFilter = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.process.VideoPassThroughFilter",    // tp_name
    sizeof(py_obj_VideoPassThroughFilter),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base = &py_type_VideoSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) VideoPassThroughFilter_dealloc,
    .tp_init = (initproc) VideoPassThroughFilter_init,
    .tp_getset = VideoPassThroughFilter_getsetters,
    .tp_methods = VideoPassThroughFilter_methods,
};

void init_VideoPassThroughFilter( PyObject *module ) {
    if( PyType_Ready( &py_type_VideoPassThroughFilter ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_VideoPassThroughFilter );
    PyModule_AddObject( module, "VideoPassThroughFilter", (PyObject *) &py_type_VideoPassThroughFilter );

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );
}



