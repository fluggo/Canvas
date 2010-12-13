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

static PyObject *pysource_funcs;

typedef struct {
    workspace_t *workspace;
    audio_source source;
} AudioWorkspace_private;

#define PRIV(obj)        ((AudioWorkspace_private*)(((void *) obj) + py_type_AudioSource.tp_basicsize))

typedef struct {
    PyObject_HEAD

    PyObject *workspace;
    workspace_item_t *item;
} py_obj_WorkspaceItem;

static int
WorkspaceItem_init( py_obj_WorkspaceItem *self, PyObject *args, PyObject *kwds ) {
    self->workspace = NULL;
    self->item = NULL;

    return 0;
}

static PyObject *
WorkspaceItem_get_x( py_obj_WorkspaceItem *self, void *closure ) {
    if( !self->item ) {
        PyErr_SetString( PyExc_RuntimeError, "This object doesn't refer to a valid item anymore." );
        return NULL;
    }

    int64_t x;
    workspace_get_item_pos( self->item, &x, NULL, NULL );

    return Py_BuildValue( "L", x );
}

static PyObject *
WorkspaceItem_get_length( py_obj_WorkspaceItem *self, void *closure ) {
    if( !self->item ) {
        PyErr_SetString( PyExc_RuntimeError, "This object doesn't refer to a valid item anymore." );
        return NULL;
    }

    int64_t length;
    workspace_get_item_pos( self->item, NULL, &length, NULL );

    return Py_BuildValue( "L", length );
}

static PyObject *
WorkspaceItem_get_offset( py_obj_WorkspaceItem *self, void *closure ) {
    if( !self->item ) {
        PyErr_SetString( PyExc_RuntimeError, "This object doesn't refer to a valid item anymore." );
        return NULL;
    }

    int64_t offset = workspace_get_item_offset( self->item );

    return Py_BuildValue( "L", offset );
}

static int
WorkspaceItem_set_offset( py_obj_WorkspaceItem *self, PyObject *value, void *closure ) {
    if( !self->item ) {
        PyErr_SetString( PyExc_RuntimeError, "This object doesn't refer to a valid item anymore." );
        return -1;
    }

    PyObject *as_long = PyNumber_Long( value );

    if( !as_long )
        return -1;

    int64_t offset = PyLong_AsLongLong( as_long );
    Py_CLEAR(as_long);

    workspace_set_item_offset( self->item, offset );
    return 0;
}

static PyObject *
WorkspaceItem_get_source( py_obj_WorkspaceItem *self, void *closure ) {
    if( !self->item ) {
        PyErr_SetString( PyExc_RuntimeError, "This object doesn't refer to a valid item anymore." );
        return NULL;
    }

    audio_source *source = workspace_get_item_source( self->item );
    PyObject *result = source->obj;

    Py_INCREF(result);
    return result;
}

static int
WorkspaceItem_set_source( py_obj_WorkspaceItem *self, PyObject *value, void *closure ) {
    if( !self->item ) {
        PyErr_SetString( PyExc_RuntimeError, "This object doesn't refer to a valid item anymore." );
        return -1;
    }

    AudioSourceHolder *holder = workspace_get_item_source( self->item );

    if( !py_audio_take_source( value, holder ) )
        return -1;

    return 0;
}

static PyObject *
WorkspaceItem_get_tag( py_obj_WorkspaceItem *self, void *closure ) {
    if( !self->item ) {
        PyErr_SetString( PyExc_RuntimeError, "This object doesn't refer to a valid item anymore." );
        return NULL;
    }

    PyObject *tag = workspace_get_item_tag( self->item );

    if( tag ) {
        Py_INCREF(tag);
        return tag;
    }

    Py_RETURN_NONE;
}

static int
WorkspaceItem_set_tag( py_obj_WorkspaceItem *self, PyObject *value, void *closure ) {
    if( !self->item ) {
        PyErr_SetString( PyExc_RuntimeError, "This object doesn't refer to a valid item anymore." );
        return -1;
    }

    PyObject *tag = workspace_get_item_tag( self->item );

    Py_INCREF(value);
    Py_DECREF(tag);

    workspace_set_item_tag( self->item, value );
    return 0;
}

static PyGetSetDef WorkspaceItem_getsetters[] = {
    { "x", (getter) WorkspaceItem_get_x, NULL, "X coordinate (start frame) of this item." },
    { "length", (getter) WorkspaceItem_get_length, NULL, "Length (duration) of this item." },
    { "source", (getter) WorkspaceItem_get_source, (setter) WorkspaceItem_set_source, "Source for this item." },
    { "offset", (getter) WorkspaceItem_get_offset, (setter) WorkspaceItem_set_offset, "Offset into the source." },
    { "tag", (getter) WorkspaceItem_get_tag, (setter) WorkspaceItem_set_tag, "Optional user tag for data on this item." },
    { NULL }
};

static PyObject *
WorkspaceItem_update( py_obj_WorkspaceItem *self, PyObject *args, PyObject *kw ) {
    if( !self->item ) {
        PyErr_SetString( PyExc_RuntimeError, "This object doesn't refer to a valid item anymore." );
        return NULL;
    }

    AudioSourceHolder *holder = workspace_get_item_source( self->item );
    PyObject *old_tag = (PyObject *) workspace_get_item_tag( self->item );

    int64_t x = 0, z = 0, length = 0, offset = 0;
    PyObject *source = NULL, *tag = old_tag;

    workspace_get_item_pos( self->item, &x, &length, &z );
    offset = workspace_get_item_offset( self->item );

    static char *kwlist[] = { "x", "length", "offset", "source", "tag", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "|LLLOO", kwlist,
            &x, &length, &z, &offset, &source, &tag ) )
        return NULL;

    if( source ) {
        if( !py_audio_take_source( source, holder ) )
            return NULL;
    }

    if( tag ) {
        Py_INCREF(tag);
        Py_DECREF(old_tag);
    }

    gpointer sourceptr = &holder->source;
    gpointer tagptr = tag;

    workspace_update_item( self->item, &x, &length, &z, &offset, &sourceptr, &tagptr );
    Py_RETURN_NONE;
}

static PyMethodDef WorkspaceItem_methods[] = {
    { "update", (PyCFunction) WorkspaceItem_update, METH_VARARGS | METH_KEYWORDS,
        "Update the properties of an item all at once. All are optional; specify x, length, offset, source, and tag as keyword parameters." },
    { NULL }
};

static void
WorkspaceItem_dealloc( py_obj_WorkspaceItem *self ) {
    Py_CLEAR( self->workspace );
    self->ob_type->tp_free( (PyObject*) self );
}

static PyObject *WorkspaceItem_richcompare( PyObject *a, PyObject *b, int op );

static PyTypeObject py_type_WorkspaceItem = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.process.AudioWorkspaceItem",    // tp_name
    sizeof(py_obj_WorkspaceItem),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) WorkspaceItem_dealloc,
    .tp_init = (initproc) WorkspaceItem_init,
    .tp_getset = WorkspaceItem_getsetters,
    .tp_methods = WorkspaceItem_methods,
    .tp_richcompare = WorkspaceItem_richcompare,
};

static PyObject *
WorkspaceItem_richcompare( PyObject *a, PyObject *b, int op ) {
    if( op != Py_EQ && op != Py_NE ) {
        PyErr_SetNone( PyExc_TypeError );
        return NULL;
    }

    if( a == b )
        Py_RETURN_TRUE;

    int cmp = PyObject_IsInstance( a, (PyObject *) &py_type_WorkspaceItem );

    if( cmp == -1 )
        return NULL;
    else if( cmp == 0 )
        Py_RETURN_FALSE;

    cmp = PyObject_IsInstance( b, (PyObject *) &py_type_WorkspaceItem );

    if( cmp == -1 )
        return NULL;
    else if( cmp == 0 )
        Py_RETURN_FALSE;

    if( ((py_obj_WorkspaceItem *) a)->item == ((py_obj_WorkspaceItem *) b)->item )
        Py_RETURN_TRUE;

    Py_RETURN_FALSE;
}

static int
AudioWorkspace_init( PyObject *self, PyObject *args, PyObject *kwds ) {
    PRIV(self)->workspace = workspace_create();
    workspace_as_audio_source( PRIV(self)->workspace, &PRIV(self)->source );

    return 0;
}

static void
AudioWorkspace_get_frame( PyObject *self, audio_frame *frame ) {
    audio_get_frame( &PRIV(self)->source, frame );
}

static void
AudioWorkspace_dealloc( PyObject *self ) {
    // Free the sources from each of the workspace items
    gint item_count = workspace_get_length( PRIV(self)->workspace );

    for( gint i = 0; i < item_count; i++ ) {
        workspace_item_t *item = workspace_get_item( PRIV(self)->workspace, i );

        AudioSourceHolder *holder = (AudioSourceHolder *) workspace_get_item_source( item );
        py_audio_take_source( NULL, holder );

        PyObject *tag = (PyObject *) workspace_get_item_tag( item );

        if( tag ) {
            Py_DECREF(tag);
        }
    }

    workspace_free( PRIV(self)->workspace );
    self->ob_type->tp_free( self );
}

static AudioFrameSourceFuncs source_funcs = {
    .getFrame = (audio_getFrameFunc) AudioWorkspace_get_frame,
};

static PyObject *
AudioWorkspace_get_funcs( PyObject *self, void *closure ) {
    Py_INCREF(pysource_funcs);
    return pysource_funcs;
}

static PyGetSetDef AudioWorkspace_getsetters[] = {
    { AUDIO_FRAME_SOURCE_FUNCS, (getter) AudioWorkspace_get_funcs, NULL, "Audio frame source C API." },
    { NULL }
};

static Py_ssize_t
AudioWorkspace_size( PyObject *self ) {
    return workspace_get_length( PRIV(self)->workspace );
}

static PyObject *
item_to_python( PyObject *self, workspace_item_t *item ) {
    py_obj_WorkspaceItem *py_item = (py_obj_WorkspaceItem *) PyObject_CallObject( (PyObject *) &py_type_WorkspaceItem, NULL );
    Py_INCREF( self );
    py_item->workspace = self;
    py_item->item = item;

    return (PyObject *) py_item;
}

static PyObject *
AudioWorkspace_getItem( PyObject *self, Py_ssize_t i ) {
    gint length = workspace_get_length( PRIV(self)->workspace );

    if( i < 0 || i >= length ) {
        PyErr_SetString( PyExc_IndexError, "Index was out of range." );
        return NULL;
    }

    return item_to_python( self, workspace_get_item( PRIV(self)->workspace, i ) );
}

static PyObject *
AudioWorkspace_add( PyObject *self, PyObject *args, PyObject *kw ) {
    int64_t x = 0, length = 0, offset = 0;
    AudioSourceHolder *holder = g_slice_new0( AudioSourceHolder );
    PyObject *source = NULL, *tag = NULL;

    static char *kwlist[] = { "source", "offset", "x", "length", "tag", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "O|LLLO", kwlist,
            &source, &offset, &x, &length, &tag ) )
        return NULL;

    if( !py_audio_take_source( source, holder ) )
        return NULL;

    if( tag )
        Py_INCREF(tag);

    return item_to_python( self, workspace_add_item( PRIV(self)->workspace, &holder->source, x, length, offset, 0, tag ) );
}

static PyObject *
AudioWorkspace_remove( PyObject *self, PyObject *args ) {
    py_obj_WorkspaceItem *item;

    if( !PyArg_ParseTuple( args, "O!", &py_type_WorkspaceItem, &item ) )
        return NULL;

    AudioSourceHolder *holder = (AudioSourceHolder *) workspace_get_item_source( item->item );
    py_audio_take_source( NULL, holder );

    PyObject *tag = (PyObject *) workspace_get_item_tag( item->item );

    if( tag ) {
        Py_DECREF(tag);
    }

    Py_CLEAR( item->workspace );
    workspace_remove_item( item->item );

    item->item = NULL;
    Py_RETURN_NONE;
}

static PySequenceMethods AudioWorkspace_sequence = {
    .sq_length = (lenfunc) AudioWorkspace_size,
    .sq_item = (ssizeargfunc) AudioWorkspace_getItem,
};

static PyMethodDef AudioWorkspace_methods[] = {
    { "add", (PyCFunction) AudioWorkspace_add, METH_VARARGS | METH_KEYWORDS,
        "Add a new item to the workspace." },
    { "remove", (PyCFunction) AudioWorkspace_remove, METH_VARARGS,
        "Remove an item from the workspace." },
    { NULL }
};

static PyTypeObject py_type_AudioWorkspace = {
    PyObject_HEAD_INIT(NULL)
    .tp_name = "fluggo.media.process.AudioWorkspace",    // tp_name
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_AudioSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AudioWorkspace_dealloc,
    .tp_init = (initproc) AudioWorkspace_init,
    .tp_getset = AudioWorkspace_getsetters,
    .tp_methods = AudioWorkspace_methods,
    .tp_as_sequence = &AudioWorkspace_sequence
};

void init_AudioWorkspace( PyObject *module ) {
    py_type_AudioWorkspace.tp_basicsize = py_type_AudioSource.tp_basicsize + sizeof(AudioWorkspace_private);

    if( PyType_Ready( &py_type_AudioWorkspace ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_AudioWorkspace );
    PyModule_AddObject( module, "AudioWorkspace", (PyObject *) &py_type_AudioWorkspace );

    pysource_funcs = PyCObject_FromVoidPtr( &source_funcs, NULL );

    if( PyType_Ready( &py_type_WorkspaceItem ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_WorkspaceItem );
}


