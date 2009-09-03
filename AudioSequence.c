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
static PyTypeObject *py_type_MutableSequence;

typedef struct {
    PyObject *tuple;
    int length, offset;                // Copied from tuple
    int startSample;
    AudioSourceHolder source;
} Element;

typedef struct {
    GArray *sequence;
    GMutex *mutex;
    int lastElement;
} AudioSequence_private;

#define PRIV(obj)              ((AudioSequence_private*)(((void *) obj) + (py_type_MutableSequence->tp_basicsize)))
#define SEQINDEX(self, i)    g_array_index( PRIV(self)->sequence, Element, i )

static int
AudioSequence_init( PyObject *self, PyObject *args, PyObject *kwds ) {
    if( py_type_MutableSequence->tp_init( (PyObject *) self, args, kwds ) < 0 )
        return -1;

    PRIV(self)->sequence = g_array_new( false, true, sizeof(Element) );
    PRIV(self)->mutex = g_mutex_new();
    PRIV(self)->lastElement = 0;

    return 0;
}

static void
AudioSequence_getFrame( PyObject *self, AudioFrame *frame ) {
    g_mutex_lock( PRIV(self)->mutex );
    if( frame->fullMaxSample < 0 || PRIV(self)->sequence->len == 0 ) {
        // No result
        g_mutex_unlock( PRIV(self)->mutex );
        frame->currentMaxSample = frame->currentMinSample - 1;
        return;
    }

    if( frame->currentMinSample < 0 )
        frame->currentMinSample = 0;

    frame->currentMaxSample = -1;

    // Find the source at the beginning of this frame
    // BJC: I realize this is O(n) worst-case, but hopefully n is small
    // and the worst-case is rare
    int i = min(PRIV(self)->lastElement, PRIV(self)->sequence->len);

    while( i < (PRIV(self)->sequence->len - 1) && frame->currentMinSample >= SEQINDEX(self, i).startSample + SEQINDEX(self, i).length )
        i++;

    while( i > 0 && frame->fullMaxSample < SEQINDEX(self, i).startSample )
        i--;

    while( i < PRIV(self)->sequence->len ) {
        PRIV(self)->lastElement = i;
        Element elem = SEQINDEX(self, i);
        AudioFrame tempFrame = {
            .channelCount = frame->channelCount,
            .fullMinSample = max(elem.startSample, frame->fullMinSample),
            .fullMaxSample = min(elem.startSample + elem.length - 1, frame->fullMaxSample),
        };
        tempFrame.currentMinSample = tempFrame.fullMinSample;
        tempFrame.currentMaxSample = tempFrame.fullMaxSample;
        tempFrame.frameData = frame->frameData +
            (tempFrame.fullMinSample - frame->fullMinSample) * frame->channelCount;

        if( elem.source.funcs ) {
            elem.source.funcs->getFrame( elem.source.source, &tempFrame );
        }
        else {
            // No result, fill with zeros
            memset( tempFrame.frameData, 0,
                (tempFrame.fullMaxSample - tempFrame.fullMinSample + 1)
                * tempFrame.channelCount * sizeof(float) );
        }

        frame->currentMaxSample = tempFrame.fullMaxSample;

        if( frame->currentMaxSample == frame->fullMaxSample )
            break;

        i++;
    }

    g_mutex_unlock( PRIV(self)->mutex );
}

static Py_ssize_t
AudioSequence_size( PyObject *self ) {
    return PRIV(self)->sequence->len;
}

static PyObject *
AudioSequence_getItem( PyObject *self, Py_ssize_t i ) {
    if( i < 0 || i >= PRIV(self)->sequence->len ) {
        PyErr_SetString( PyExc_IndexError, "Index was out of range." );
        return NULL;
    }

    PyObject *result =
        g_array_index( PRIV(self)->sequence, Element, i ).tuple;

    Py_INCREF( result );
    return result;
}

static PyObject *
AudioSequence_getStartSample( PyObject *self, PyObject *args ) {
    Py_ssize_t i;

    if( !PyArg_ParseTuple( args, "n", &i ) )
        return NULL;

    if( i < 0 || i >= PRIV(self)->sequence->len ) {
        PyErr_SetString( PyExc_IndexError, "Index was out of range." );
        return NULL;
    }

    return Py_BuildValue( "i",
        g_array_index( PRIV(self)->sequence, Element, i ).startSample );
}

static int
_setItem( PyObject *self, Py_ssize_t i, PyObject *v ) {
    PyObject *sourceObj;
    int length, offset;
    AudioSourceHolder source = { NULL };

    // Parse everything and make sure it's okay
    if( !PyArg_ParseTuple( v, "Oii", &sourceObj, &offset, &length ) )
        return -1;

    if( length < 1 ) {
        PyErr_SetString( PyExc_ValueError, "Length cannot be less than one." );
        return -1;
    }

    if( !takeAudioSource( sourceObj, &source ) )
        return -1;

    Py_INCREF( v );

    // Replace the current holder
    Element *entry = &g_array_index( PRIV(self)->sequence, Element, i );
    Py_CLEAR( entry->tuple );
    takeAudioSource( NULL, &entry->source );

    int lengthAdjust = length - entry->length;

    entry->source = source;
    entry->tuple = v;
    entry->length = length;
    entry->offset = offset;

    if( i != 0 && entry->startSample == 0 ) {
        Element *prevEntry = &g_array_index( PRIV(self)->sequence, Element, i - 1 );
        entry->startSample = prevEntry->startSample + prevEntry->length;
    }

    if( lengthAdjust != 0 ) {
        for( int j = i + 1; j < PRIV(self)->sequence->len; j++ )
            g_array_index( PRIV(self)->sequence, Element, j ).startSample += lengthAdjust;
    }

    return 0;
}

static int
AudioSequence_setItem( PyObject *self, Py_ssize_t i, PyObject *v ) {
    g_mutex_lock( PRIV(self)->mutex );
    int result = _setItem( self, i, v );
    g_mutex_unlock( PRIV(self)->mutex );

    return result;
}

static PyObject *
AudioSequence_insert( PyObject *self, PyObject *args ) {
    Py_ssize_t i;
    PyObject *v;

    if( !PyArg_ParseTuple( args, "nO", &i, &v ) )
        return NULL;

    if( i < 0 )
        i += PRIV(self)->sequence->len;

    if( i < 0 )
        i = 0;

    if( i > PRIV(self)->sequence->len ) {
        PyErr_SetString( PyExc_IndexError, "Index was out of range." );
        return NULL;
    }

    // Open up a slot
    Element empty = { NULL };

    g_mutex_lock( PRIV(self)->mutex );
    g_array_insert_val( PRIV(self)->sequence, i, empty );

    // Set the slot
    if( _setItem( self, i, v ) < 0 ) {
        g_mutex_unlock( PRIV(self)->mutex );
        return NULL;
    }

    g_mutex_unlock( PRIV(self)->mutex );
    Py_RETURN_NONE;
}

static void
AudioSequence_dealloc( PyObject *self ) {
    for( int i = 0; i < PRIV(self)->sequence->len; i++ ) {
        Element *element = &g_array_index( PRIV(self)->sequence, Element, i );

        Py_CLEAR( element->tuple );
        takeAudioSource( NULL, &element->source );
    }

    g_array_free( PRIV(self)->sequence, true );
    g_mutex_free( PRIV(self)->mutex );

    self->ob_type->tp_free( (PyObject*) self );
}

static AudioFrameSourceFuncs sourceFuncs = {
    0,
    (audio_getFrameFunc) AudioSequence_getFrame
};

static PyObject *
AudioSequence_getFuncs( PyObject *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef AudioSequence_getsetters[] = {
    { "_audioFrameSourceFuncs", (getter) AudioSequence_getFuncs, NULL, "Audio frame source C API." },
    { NULL }
};

static PySequenceMethods AudioSequence_sequence = {
    .sq_length = (lenfunc) AudioSequence_size,
    .sq_item = (ssizeargfunc) AudioSequence_getItem,
    .sq_ass_item = (ssizeobjargproc) AudioSequence_setItem
};

static PyMethodDef AudioSequence_methods[] = {
    { "insert", (PyCFunction) AudioSequence_insert, METH_VARARGS,
        "Inserts a new element into the sequence." },
    { "getStartSample", (PyCFunction) AudioSequence_getStartSample, METH_VARARGS,
        "Gets the starting sample for an element." },
    { NULL }
};

static PyTypeObject py_type_AudioSequence = {
    PyObject_HEAD_INIT(NULL)
    .tp_name = "fluggo.media.AudioSequence",    // tp_name
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_dealloc = (destructor) AudioSequence_dealloc,
    .tp_init = (initproc) AudioSequence_init,
    .tp_getset = AudioSequence_getsetters,
    .tp_methods = AudioSequence_methods,
    .tp_as_sequence = &AudioSequence_sequence
};

void init_AudioSequence( PyObject *module ) {
    PyObject *collections = PyImport_ImportModule( "collections" );
    py_type_MutableSequence = (PyTypeObject*) PyObject_GetAttrString( collections, "MutableSequence" );

    Py_CLEAR( collections );

    py_type_AudioSequence.tp_base = py_type_MutableSequence;
    py_type_AudioSequence.tp_basicsize = py_type_MutableSequence->tp_basicsize +
        sizeof(AudioSequence_private);

    if( PyType_Ready( &py_type_AudioSequence ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_AudioSequence );
    PyModule_AddObject( module, "AudioSequence", (PyObject *) &py_type_AudioSequence );

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );
}



