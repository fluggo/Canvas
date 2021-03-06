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

static PyObject *pysource_funcs;

#define PRIV(obj)        ((audio_frame*)(((void *) obj) + py_type_AudioSource.tp_basicsize))

static void
AudioFrame_get_frame( PyObject *self, audio_frame *frame ) {
    audio_copy_frame( frame, PRIV(self), 0 );
}

static void
AudioFrame_dealloc( PyObject *self ) {
    PyMem_Free( PRIV(self)->data );
    self->ob_type->tp_free( self );
}

static AudioFrameSourceFuncs source_funcs = {
    .getFrame = (audio_getFrameFunc) AudioFrame_get_frame,
};

static PyObject *
AudioFrame_get_funcs( PyObject *self, void *closure ) {
    Py_INCREF(pysource_funcs);
    return pysource_funcs;
}

static PyObject *
AudioFrame_get_full_min_sample( PyObject *self, void *closure ) {
    return PyLong_FromLong( PRIV(self)->full_min_sample );
}

static PyObject *
AudioFrame_get_full_max_sample( PyObject *self, void *closure ) {
    return PyLong_FromLong( PRIV(self)->full_max_sample );
}

static PyObject *
AudioFrame_get_current_min_sample( PyObject *self, void *closure ) {
    return PyLong_FromLong( PRIV(self)->current_min_sample );
}

static PyObject *
AudioFrame_get_current_max_sample( PyObject *self, void *closure ) {
    return PyLong_FromLong( PRIV(self)->current_max_sample );
}

static PyObject *
AudioFrame_get_channels( PyObject *self, void *closure ) {
    return PyLong_FromLong( PRIV(self)->channels );
}

static PyGetSetDef AudioFrame_getsetters[] = {
    { AUDIO_FRAME_SOURCE_FUNCS, (getter) AudioFrame_get_funcs, NULL, "Audio frame source C API." },
    { "full_min_sample", (getter) AudioFrame_get_full_min_sample, NULL, "The first allocated sample of this frame." },
    { "full_max_sample", (getter) AudioFrame_get_full_max_sample, NULL, "The last allocated sample of this frame." },
    { "current_min_sample", (getter) AudioFrame_get_current_min_sample, NULL, "The first sample of this frame with defined data in it." },
    { "current_max_sample", (getter) AudioFrame_get_current_max_sample, NULL, "The last sample of this frame with defined data in it." },
    { "channels", (getter) AudioFrame_get_channels, NULL, "The number of channels in this frame. Unused channels will be filled with zeroes." },
    { NULL }
};

// Sequence protocol for raw data
static Py_ssize_t
AudioFrame_size( PyObject *self ) {
    return (PRIV(self)->full_max_sample - PRIV(self)->full_min_sample + 1) * PRIV(self)->channels;
}

static PyObject *
AudioFrame_get_item( PyObject *self, Py_ssize_t i ) {
    if( i < 0 || i >= (PRIV(self)->full_max_sample - PRIV(self)->full_min_sample + 1) * PRIV(self)->channels ) {
        PyErr_SetString( PyExc_IndexError, "Index was out of range." );
        return NULL;
    }

    return PyFloat_FromDouble( PRIV(self)->data[i] );
}

static PySequenceMethods AudioFrame_sequence = {
    .sq_length = (lenfunc) AudioFrame_size,
    .sq_item = (ssizeargfunc) AudioFrame_get_item,
};

static PyObject *
AudioFrame_sample( PyObject *self, PyObject *args ) {
    int sample, channel;

    if( !PyArg_ParseTuple( args, "ii", &sample, &channel ) )
        return NULL;

    if( sample < PRIV(self)->current_min_sample || sample > PRIV(self)->current_max_sample )
        Py_RETURN_NONE;

    if( channel < 0 || channel >= PRIV(self)->channels ) {
        PyErr_SetString( PyExc_IndexError, "Channel index was out of range." );
        return NULL;
    }

    return PyFloat_FromDouble( *audio_get_sample( PRIV(self), sample, channel ) );
}

static PyMethodDef AudioFrame_methods[] = {
    { "sample", (PyCFunction) AudioFrame_sample, METH_VARARGS,
        "sample(sample, channel): Get the sample at the given index and channel. If sample is beyond the range of this frame, None is returned." },
    { NULL }
};

static PyTypeObject py_type_AudioFrame = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.AudioFrame",    // tp_name
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_base = &py_type_AudioSource,
    .tp_dealloc = (destructor) AudioFrame_dealloc,
    .tp_getset = AudioFrame_getsetters,
    .tp_methods = AudioFrame_methods,
    .tp_as_sequence = &AudioFrame_sequence
};

/*
    Function: py_AudioFrame_new
        Creates an AudioFrame and gets a pointer to its audio_frame structure.
        Internal use only.

    Parameters:
        min_sample - Minimum sample for this frame.
        max_sample - Maximum sample for this frame.
        channels - Number of channels for this frame.
        frame - Optional pointer to a variable to receive the actual frame.
            Everything but the current data window will be set.

    Returns:
        A reference to the new object if successful, or NULL on an error (an
        exception will be set).
*/
PyObject *
py_AudioFrame_new( int min_sample, int max_sample, int channels, audio_frame **frame ) {
    if( max_sample < min_sample ) {
        PyErr_SetString( PyExc_ValueError, "max_sample was less than min_sample." );
        return NULL;
    }

    if( channels < 0 ) {
        PyErr_SetString( PyExc_ValueError, "channels was less than zero." );
        return NULL;
    }

    PyObject *tuple = PyTuple_New( 0 ), *dict = PyDict_New();

    PyObject *result = py_type_AudioFrame.tp_new( &py_type_AudioFrame, tuple, dict );

    Py_CLEAR(tuple);
    Py_CLEAR(dict);

    if( !result )
        return NULL;

    PRIV(result)->full_min_sample = min_sample;
    PRIV(result)->full_max_sample = max_sample;
    PRIV(result)->channels = channels;

    PRIV(result)->data = PyMem_Malloc( sizeof(float) * (max_sample - min_sample + 1) * channels );

    if( !PRIV(result)->data ) {
        Py_DECREF(result);
        return PyErr_NoMemory();
    }

    if( frame )
        *frame = PRIV(result);

    return result;
}

void init_AudioFrame( PyObject *module ) {
    py_type_AudioFrame.tp_basicsize = py_type_AudioSource.tp_basicsize + sizeof(audio_frame);

    if( PyType_Ready( &py_type_AudioFrame ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_AudioFrame );
    PyModule_AddObject( module, "AudioFrame", (PyObject *) &py_type_AudioFrame );

    pysource_funcs = PyCapsule_New( &source_funcs, AUDIO_FRAME_SOURCE_FUNCS, NULL );
}


