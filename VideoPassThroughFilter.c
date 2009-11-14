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

    if( !takeVideoSource( source, &self->source ) )
        return -1;

    return 0;
}

static void
VideoPassThroughFilter_getFrame( py_obj_VideoPassThroughFilter *self, int frameIndex, rgba_f16_frame *frame ) {
    if( self->source.source == NULL ) {
        // No result
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

    getFrame_f16( &self->source, frameIndex, frame );
}

static void
VideoPassThroughFilter_getFrame32( py_obj_VideoPassThroughFilter *self, int frameIndex, rgba_f32_frame *frame ) {
    if( self->source.source == NULL ) {
        // No result
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

    getFrame_f32( &self->source, frameIndex, frame );
}

static void
VideoPassThroughFilter_getFrameGL( py_obj_VideoPassThroughFilter *self, int frameIndex, rgba_gl_frame *frame ) {
    if( self->source.source == NULL ) {
        // No result
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

    getFrame_gl( &self->source, frameIndex, frame );
}

static void
VideoPassThroughFilter_dealloc( py_obj_VideoPassThroughFilter *self ) {
    takeVideoSource( NULL, &self->source );
    self->ob_type->tp_free( (PyObject*) self );
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
    { "_videoFrameSourceFuncs", (getter) VideoPassThroughFilter_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PyTypeObject py_type_VideoPassThroughFilter = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.VideoPassThroughFilter",    // tp_name
    sizeof(py_obj_VideoPassThroughFilter),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) VideoPassThroughFilter_dealloc,
    .tp_init = (initproc) VideoPassThroughFilter_init,
    .tp_getset = VideoPassThroughFilter_getsetters
};

void init_VideoPassThroughFilter( PyObject *module ) {
    if( PyType_Ready( &py_type_VideoPassThroughFilter ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_VideoPassThroughFilter );
    PyModule_AddObject( module, "VideoPassThroughFilter", (PyObject *) &py_type_VideoPassThroughFilter );

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );
}



