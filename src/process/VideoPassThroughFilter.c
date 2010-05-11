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

#include "framework.h"

static PyObject *pysourceFuncs;

typedef struct {
    PyObject_HEAD

    VideoSourceHolder source;
} py_obj_VideoPassThroughFilter;

static int
VideoPassThroughFilter_init( py_obj_VideoPassThroughFilter *self, PyObject *args, PyObject *kwds ) {
    PyObject *source;

    if( !PyArg_ParseTuple( args, "O", &source ) )
        return -1;

    if( !py_video_takeSource( source, &self->source ) )
        return -1;

    return 0;
}

static void
VideoPassThroughFilter_getFrame( py_obj_VideoPassThroughFilter *self, int frameIndex, rgba_frame_f16 *frame ) {
    if( self->source.source.obj == NULL ) {
        // No result
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

    getFrame_f16( &self->source.source, frameIndex, frame );
}

static void
VideoPassThroughFilter_getFrame32( py_obj_VideoPassThroughFilter *self, int frameIndex, rgba_frame_f32 *frame ) {
    if( self->source.source.obj == NULL ) {
        // No result
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

    getFrame_f32( &self->source.source, frameIndex, frame );
}

static void
VideoPassThroughFilter_getFrameGL( py_obj_VideoPassThroughFilter *self, int frameIndex, rgba_frame_gl *frame ) {
    if( self->source.source.obj == NULL ) {
        // No result
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

    getFrame_gl( &self->source.source, frameIndex, frame );
}

static void
VideoPassThroughFilter_dealloc( py_obj_VideoPassThroughFilter *self ) {
    py_video_takeSource( NULL, &self->source );
    self->ob_type->tp_free( (PyObject*) self );
}

static PyObject *
VideoPassThroughFilter_getSource( py_obj_VideoPassThroughFilter *self ) {
    if( self->source.source.obj == NULL )
        Py_RETURN_NONE;

    Py_INCREF((PyObject *) self->source.source.obj);
    return (PyObject *) self->source.source.obj;
}

static PyObject *
VideoPassThroughFilter_setSource( py_obj_VideoPassThroughFilter *self, PyObject *args, void *closure ) {
    PyObject *source;

    if( !PyArg_ParseTuple( args, "O", &source ) )
        return NULL;

    if( !py_video_takeSource( source, &self->source ) )
        return NULL;

    Py_RETURN_NONE;
}

static VideoFrameSourceFuncs sourceFuncs = {
    .getFrame = (video_getFrameFunc) VideoPassThroughFilter_getFrame,
    .getFrame32 = (video_getFrame32Func) VideoPassThroughFilter_getFrame32,
    .getFrameGL = (video_getFrameGLFunc) VideoPassThroughFilter_getFrameGL
};

static PyObject *
VideoPassThroughFilter_getFuncs( py_obj_VideoPassThroughFilter *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef VideoPassThroughFilter_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) VideoPassThroughFilter_getFuncs, NULL, "Video frame source C API." },
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



