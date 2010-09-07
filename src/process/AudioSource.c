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

PyObject *py_AudioFrame_new( int min_sample, int max_sample, int channels, audio_frame **frame );

static PyObject *
py_audio_get_frame( PyObject *self, PyObject *args, PyObject *kw ) {
    int min_sample, max_sample, channels;

    static char *kwlist[] = { "min_sample", "max_sample", "channels", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "iii", kwlist,
            &min_sample, &max_sample, &channels ) )
        return NULL;

    audio_frame *frame;
    PyObject *result = py_AudioFrame_new( min_sample, max_sample, channels, &frame );

    if( !result )
        return NULL;

    AudioSourceHolder source = { .csource = NULL };

    if( !py_audio_take_source( self, &source ) ) {
        Py_DECREF(result);
        return NULL;
    }

    audio_get_frame( &source.source, frame );

    if( !py_audio_take_source( NULL, &source ) ) {
        Py_DECREF(result);
        return NULL;
    }

    return result;
}

static PyMethodDef AudioSource_methods[] = {
    { "get_frame", (PyCFunction) py_audio_get_frame, METH_VARARGS | METH_KEYWORDS,
        "Get a frame of audio.\n"
        "\n"
        "(AudioFrame) frame = source.get_frame(min_sample, max_sample, channels)" },
    { NULL }
};

EXPORT PyTypeObject py_type_AudioSource = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.process.AudioSource",    // tp_name
    0,    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_methods = AudioSource_methods,
};

void init_AudioSource( PyObject *module ) {
    if( PyType_Ready( &py_type_AudioSource ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_AudioSource );
    PyModule_AddObject( module, "AudioSource", (PyObject *) &py_type_AudioSource );
}


