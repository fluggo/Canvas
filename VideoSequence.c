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
    int startFrame;
    VideoSourceHolder source;
} Element;

typedef struct {
    GArray *sequence;
    GMutex *mutex;
    int lastElement;
} VideoSequence_private;

#define PRIV(obj)              ((VideoSequence_private*)(((void *) obj) + (py_type_MutableSequence->tp_basicsize)))
#define SEQINDEX(self, i)    g_array_index( PRIV(self)->sequence, Element, i )

static int
VideoSequence_init( PyObject *self, PyObject *args, PyObject *kwds ) {
    if( py_type_MutableSequence->tp_init( (PyObject *) self, args, kwds ) < 0 )
        return -1;

    PRIV(self)->sequence = g_array_new( false, true, sizeof(Element) );
    PRIV(self)->mutex = g_mutex_new();
    PRIV(self)->lastElement = 0;

    return 0;
}

static Element *
pickElement_nolock( PyObject *self, int frameIndex ) {
    if( frameIndex < 0 || PRIV(self)->sequence->len == 0 )
        return NULL;

    // Find the source
    // BJC: I realize this is O(n) worst-case, but hopefully n is small
    // and the worst-case is rare
    int i = min(PRIV(self)->lastElement, PRIV(self)->sequence->len);

    while( i < (PRIV(self)->sequence->len - 1) && frameIndex >= SEQINDEX(self, i).startFrame + SEQINDEX(self, i).length )
        i++;

    while( i > 0 && frameIndex < SEQINDEX(self, i).startFrame )
        i--;

    PRIV(self)->lastElement = i;

    Element *elem = &SEQINDEX(self, i);

    if( !elem->source.funcs || elem->startFrame + elem->length < frameIndex )
        return NULL;

    return elem;
}

static void
VideoSequence_getFrame( PyObject *self, int frameIndex, rgba_f16_frame *frame ) {
    g_mutex_lock( PRIV(self)->mutex );
    Element *elemPtr = pickElement_nolock( self, frameIndex );

    if( !elemPtr ) {
        // No result
        g_mutex_unlock( PRIV(self)->mutex );
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

    Element elem = *elemPtr;
    g_mutex_unlock( PRIV(self)->mutex );

    getFrame_f16( &elem.source, frameIndex - elem.startFrame + elem.offset, frame );
}

static void
VideoSequence_getFrame32( PyObject *self, int frameIndex, rgba_f32_frame *frame ) {
    g_mutex_lock( PRIV(self)->mutex );
    Element *elemPtr = pickElement_nolock( self, frameIndex );

    if( !elemPtr ) {
        // No result
        g_mutex_unlock( PRIV(self)->mutex );
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

    Element elem = *elemPtr;
    g_mutex_unlock( PRIV(self)->mutex );

    getFrame_f32( &elem.source, frameIndex - elem.startFrame + elem.offset, frame );
}

static void
VideoSequence_getFrameGL( PyObject *self, int frameIndex, rgba_gl_frame *frame ) {
    g_mutex_lock( PRIV(self)->mutex );
    Element *elemPtr = pickElement_nolock( self, frameIndex );

    if( !elemPtr ) {
        // No result
        g_mutex_unlock( PRIV(self)->mutex );
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

    Element elem = *elemPtr;
    g_mutex_unlock( PRIV(self)->mutex );

    getFrame_gl( &elem.source, frameIndex - elem.startFrame + elem.offset, frame );
}

static Py_ssize_t
VideoSequence_size( PyObject *self ) {
    return PRIV(self)->sequence->len;
}

static PyObject *
VideoSequence_getItem( PyObject *self, Py_ssize_t i ) {
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
VideoSequence_getStartFrame( PyObject *self, PyObject *args ) {
    Py_ssize_t i;

    if( !PyArg_ParseTuple( args, "n", &i ) )
        return NULL;

    if( i < 0 || i >= PRIV(self)->sequence->len ) {
        PyErr_SetString( PyExc_IndexError, "Index was out of range." );
        return NULL;
    }

    return Py_BuildValue( "i",
        g_array_index( PRIV(self)->sequence, Element, i ).startFrame );
}

static int
_setItem( PyObject *self, Py_ssize_t i, PyObject *v ) {
    PyObject *sourceObj;
    int length, offset;
    VideoSourceHolder source = { NULL };

    // Parse everything and make sure it's okay
    if( !PyArg_ParseTuple( v, "Oii", &sourceObj, &offset, &length ) )
        return -1;

    if( length < 1 ) {
        PyErr_SetString( PyExc_ValueError, "Length cannot be less than one." );
        return -1;
    }

    if( !takeVideoSource( sourceObj, &source ) )
        return -1;

    Py_INCREF( v );

    // Replace the current holder
    Element *entry = &g_array_index( PRIV(self)->sequence, Element, i );
    Py_CLEAR( entry->tuple );
    takeVideoSource( NULL, &entry->source );

    int lengthAdjust = length - entry->length;

    entry->source = source;
    entry->tuple = v;
    entry->length = length;
    entry->offset = offset;

    if( i != 0 && entry->startFrame == 0 ) {
        Element *prevEntry = &g_array_index( PRIV(self)->sequence, Element, i - 1 );
        entry->startFrame = prevEntry->startFrame + prevEntry->length;
    }

    if( lengthAdjust != 0 ) {
        for( int j = i + 1; j < PRIV(self)->sequence->len; j++ )
            g_array_index( PRIV(self)->sequence, Element, j ).startFrame += lengthAdjust;
    }

    return 0;
}

static int
VideoSequence_setItem( PyObject *self, Py_ssize_t i, PyObject *v ) {
    g_mutex_lock( PRIV(self)->mutex );
    int result = _setItem( self, i, v );
    g_mutex_unlock( PRIV(self)->mutex );

    return result;
}

static PyObject *
VideoSequence_insert( PyObject *self, PyObject *args ) {
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
VideoSequence_dealloc( PyObject *self ) {
    for( int i = 0; i < PRIV(self)->sequence->len; i++ ) {
        Element *element = &g_array_index( PRIV(self)->sequence, Element, i );

        Py_CLEAR( element->tuple );
        takeVideoSource( NULL, &element->source );
    }

    g_array_free( PRIV(self)->sequence, true );
    g_mutex_free( PRIV(self)->mutex );

    self->ob_type->tp_free( (PyObject*) self );
}

static VideoFrameSourceFuncs sourceFuncs = {
    .getFrame = (video_getFrameFunc) VideoSequence_getFrame,
    .getFrame32 = (video_getFrame32Func) VideoSequence_getFrame32,
    .getFrameGL = (video_getFrameGLFunc) VideoSequence_getFrameGL,
};

static PyObject *
VideoSequence_getFuncs( PyObject *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef VideoSequence_getsetters[] = {
    { "_videoFrameSourceFuncs", (getter) VideoSequence_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PySequenceMethods VideoSequence_sequence = {
    .sq_length = (lenfunc) VideoSequence_size,
    .sq_item = (ssizeargfunc) VideoSequence_getItem,
    .sq_ass_item = (ssizeobjargproc) VideoSequence_setItem
};

static PyMethodDef VideoSequence_methods[] = {
    { "insert", (PyCFunction) VideoSequence_insert, METH_VARARGS,
        "Inserts a new element into the sequence." },
    { "getStartFrame", (PyCFunction) VideoSequence_getStartFrame, METH_VARARGS,
        "Gets the starting frame for an element." },
    { NULL }
};

static PyTypeObject py_type_VideoSequence = {
    PyObject_HEAD_INIT(NULL)
    .tp_name = "fluggo.media.VideoSequence",    // tp_name
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_dealloc = (destructor) VideoSequence_dealloc,
    .tp_init = (initproc) VideoSequence_init,
    .tp_getset = VideoSequence_getsetters,
    .tp_methods = VideoSequence_methods,
    .tp_as_sequence = &VideoSequence_sequence
};

void init_VideoSequence( PyObject *module ) {
    PyObject *collections = PyImport_ImportModule( "collections" );
    py_type_MutableSequence = (PyTypeObject*) PyObject_GetAttrString( collections, "MutableSequence" );

    Py_CLEAR( collections );

    py_type_VideoSequence.tp_base = py_type_MutableSequence;
    py_type_VideoSequence.tp_basicsize = py_type_MutableSequence->tp_basicsize +
        sizeof(VideoSequence_private);

    if( PyType_Ready( &py_type_VideoSequence ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_VideoSequence );
    PyModule_AddObject( module, "VideoSequence", (PyObject *) &py_type_VideoSequence );

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );
}



