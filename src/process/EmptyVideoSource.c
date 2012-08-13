/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2009-10 Brian J. Crowell <brian@fluggo.com>

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

static int
EmptyVideoSource_init( PyObject *self, PyObject *args, PyObject *kwds ) {
    return 0;
}

static void
EmptyVideoSource_getFrame( PyObject *self, int frameIndex, rgba_frame_f16 *frame ) {
    box2i_set_empty( &frame->current_window );
}

static void
EmptyVideoSource_getFrame32( PyObject *self, int frameIndex, rgba_frame_f32 *frame ) {
    box2i_set_empty( &frame->current_window );
}

static void
EmptyVideoSource_getFrameGL( PyObject *self, int frameIndex, rgba_frame_gl *frame ) {
    video_get_frame_gl( NULL, 0, frame );
}

static void
EmptyVideoSource_dealloc( PyObject *self ) {
    self->ob_type->tp_free( self );
}

static video_frame_source_funcs sourceFuncs = {
    .get_frame = (video_get_frame_func) EmptyVideoSource_getFrame,
    .get_frame_32 = (video_get_frame_32_func) EmptyVideoSource_getFrame32,
    .get_frame_gl = (video_get_frame_gl_func) EmptyVideoSource_getFrameGL
};

static PyObject *
EmptyVideoSource_getFuncs( PyObject *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef EmptyVideoSource_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) EmptyVideoSource_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PyTypeObject py_type_EmptyVideoSource = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.EmptyVideoSource",
    .tp_basicsize = sizeof(PyObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_VideoSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) EmptyVideoSource_dealloc,
    .tp_init = (initproc) EmptyVideoSource_init,
    .tp_getset = EmptyVideoSource_getsetters,
};

void init_EmptyVideoSource( PyObject *module ) {
    if( PyType_Ready( &py_type_EmptyVideoSource ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_EmptyVideoSource );
    PyModule_AddObject( module, "EmptyVideoSource", (PyObject *) &py_type_EmptyVideoSource );

    pysourceFuncs = PyCapsule_New( &sourceFuncs, VIDEO_FRAME_SOURCE_FUNCS, NULL );
}



