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

PyObject *py_get_frame_f16( PyObject *self, PyObject *args, PyObject *kw );
PyObject *py_get_frame_f32( PyObject *self, PyObject *args, PyObject *kw );

static PyMethodDef VideoSource_methods[] = {
    { "get_frame_f16", (PyCFunction) py_get_frame_f16, METH_VARARGS | METH_KEYWORDS,
        "Get a frame of video from a video source.\n"
        "\n"
        "(RgbaFrameF16) frame = source.get_frame_f16(frame_index, data_window)" },
    { "get_frame_f32", (PyCFunction) py_get_frame_f32, METH_VARARGS | METH_KEYWORDS,
        "Get a frame of video from a video source.\n"
        "\n"
        "(RgbaFrameF32) frame = source.get_frame_f32(frame_index, data_window)" },
    { NULL }
};

EXPORT PyTypeObject py_type_VideoSource = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.VideoSource",
    .tp_basicsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_methods = VideoSource_methods,
};

void init_VideoSource( PyObject *module ) {
    if( PyType_Ready( &py_type_VideoSource ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_VideoSource );
    PyModule_AddObject( module, "VideoSource", (PyObject *) &py_type_VideoSource );
}


