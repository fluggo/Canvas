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
    video_source source;
} Workspace_private;

#define PRIV(obj)        ((Workspace_private*)(((void *) obj) + py_type_VideoSource.tp_basicsize))

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
WorkspaceItem_get_width( py_obj_WorkspaceItem *self, void *closure ) {
    if( !self->item ) {
        PyErr_SetString( PyExc_RuntimeError, "This object doesn't refer to a valid item anymore." );
        return NULL;
    }

    int64_t width;
    workspace_get_item_pos( self->item, NULL, &width, NULL );

    return Py_BuildValue( "L", width );
}

static PyObject *
WorkspaceItem_get_z( py_obj_WorkspaceItem *self, void *closure ) {
    if( !self->item ) {
        PyErr_SetString( PyExc_RuntimeError, "This object doesn't refer to a valid item anymore." );
        return NULL;
    }

    int64_t z;
    workspace_get_item_pos( self->item, NULL, NULL, &z );

    return Py_BuildValue( "L", z );
}

static int
WorkspaceItem_set_z( py_obj_WorkspaceItem *self, PyObject *value, void *closure ) {
    if( !self->item ) {
        PyErr_SetString( PyExc_RuntimeError, "This object doesn't refer to a valid item anymore." );
        return -1;
    }

    PyObject *as_long = PyNumber_Long( value );

    if( !as_long )
        return -1;

    int64_t z = PyLong_AsLongLong( as_long );
    Py_CLEAR(as_long);

    workspace_update_item( self->item, NULL, NULL, &z, NULL, NULL, NULL );
    return 0;
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

    video_source *source = workspace_get_item_source( self->item );
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

    VideoSourceHolder *holder = workspace_get_item_source( self->item );

    if( !py_video_takeSource( value, holder ) )
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
    { "width", (getter) WorkspaceItem_get_width, NULL, "Width (duration) of this item." },
    { "z", (getter) WorkspaceItem_get_z, (setter) WorkspaceItem_set_z, "Z coordinate (sort order) of this item. Higher-Z items composite on top of lower-Z items." },
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

    VideoSourceHolder *holder = workspace_get_item_source( self->item );
    PyObject *old_tag = (PyObject *) workspace_get_item_tag( self->item );

    int64_t x = 0, z = 0, width = 0, offset = 0;
    PyObject *source = NULL, *tag = old_tag;

    workspace_get_item_pos( self->item, &x, &width, &z );
    offset = workspace_get_item_offset( self->item );

    static char *kwlist[] = { "x", "width", "z", "offset", "source", "tag", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "|LLLLOO", kwlist,
            &x, &width, &z, &offset, &source, &tag ) )
        return NULL;

    if( source ) {
        if( !py_video_takeSource( source, holder ) )
            return NULL;
    }

    if( tag ) {
        Py_INCREF(tag);
        Py_DECREF(old_tag);
    }

    gpointer sourceptr = &holder->source;
    gpointer tagptr = tag;

    workspace_update_item( self->item, &x, &width, &z, &offset, &sourceptr, &tagptr );
    Py_RETURN_NONE;
}

static PyMethodDef WorkspaceItem_methods[] = {
    { "update", (PyCFunction) WorkspaceItem_update, METH_VARARGS | METH_KEYWORDS,
        "Update the properties of an item all at once. All are optional; specify x, width, z, offset, source, and tag as keyword parameters." },
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
    "fluggo.media.process.VideoWorkspaceItem",    // tp_name
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
Workspace_init( PyObject *self, PyObject *args, PyObject *kwds ) {
    PRIV(self)->workspace = workspace_create_video();
    workspace_as_video_source( PRIV(self)->workspace, &PRIV(self)->source );

    return 0;
}

static void
Workspace_getFrame32( PyObject *self, int frame_index, rgba_frame_f32 *frame ) {
    video_getFrame_f32( &PRIV(self)->source, frame_index, frame );
}

static void
Workspace_dealloc( PyObject *self ) {
    // Free the sources from each of the workspace items
    gint item_count = workspace_get_length( PRIV(self)->workspace );

    for( gint i = 0; i < item_count; i++ ) {
        workspace_item_t *item = workspace_get_item( PRIV(self)->workspace, i );

        VideoSourceHolder *holder = (VideoSourceHolder *) workspace_get_item_source( item );
        py_video_takeSource( NULL, holder );

        PyObject *tag = (PyObject *) workspace_get_item_tag( item );

        if( tag )
            Py_DECREF(tag);
    }

    workspace_free( PRIV(self)->workspace );
    self->ob_type->tp_free( self );
}

static VideoFrameSourceFuncs source_funcs = {
    .getFrame32 = (video_getFrame32Func) Workspace_getFrame32,
};

static PyObject *
Workspace_get_funcs( PyObject *self, void *closure ) {
    Py_INCREF(pysource_funcs);
    return pysource_funcs;
}

static PyGetSetDef Workspace_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) Workspace_get_funcs, NULL, "Video frame source C API." },
    { NULL }
};

static Py_ssize_t
Workspace_size( PyObject *self ) {
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
Workspace_getItem( PyObject *self, Py_ssize_t i ) {
    gint length = workspace_get_length( PRIV(self)->workspace );

    if( i < 0 || i >= length ) {
        PyErr_SetString( PyExc_IndexError, "Index was out of range." );
        return NULL;
    }

    return item_to_python( self, workspace_get_item( PRIV(self)->workspace, i ) );
}

static PyObject *
Workspace_add( PyObject *self, PyObject *args, PyObject *kw ) {
    int64_t x = 0, z = 0, width = 0, offset = 0;
    VideoSourceHolder *holder = g_slice_new0( VideoSourceHolder );
    PyObject *source = NULL, *tag = NULL;

    static char *kwlist[] = { "source", "offset", "x", "width", "z", "tag", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "O|LLLLO", kwlist,
            &source, &offset, &x, &width, &z, &tag ) )
        return NULL;

    if( !py_video_takeSource( source, holder ) )
        return NULL;

    if( tag )
        Py_INCREF(tag);

    return item_to_python( self, workspace_add_item( PRIV(self)->workspace, &holder->source, x, width, offset, z, tag ) );
}

static PyObject *
Workspace_remove( PyObject *self, PyObject *args ) {
    py_obj_WorkspaceItem *item;

    if( !PyArg_ParseTuple( args, "O!", &py_type_WorkspaceItem, &item ) )
        return NULL;

    VideoSourceHolder *holder = (VideoSourceHolder *) workspace_get_item_source( item->item );
    py_video_takeSource( NULL, holder );

    PyObject *tag = (PyObject *) workspace_get_item_tag( item->item );

    if( tag )
        Py_DECREF(tag);

    Py_CLEAR( item->workspace );
    workspace_remove_item( item->item );

    item->item = NULL;
    Py_RETURN_NONE;
}

static PySequenceMethods Workspace_sequence = {
    .sq_length = (lenfunc) Workspace_size,
    .sq_item = (ssizeargfunc) Workspace_getItem,
};

static PyMethodDef Workspace_methods[] = {
    { "add", (PyCFunction) Workspace_add, METH_VARARGS | METH_KEYWORDS,
        "Add a new item to the workspace." },
    { "remove", (PyCFunction) Workspace_remove, METH_VARARGS,
        "Remove an item from the workspace." },
    { NULL }
};

static PyTypeObject py_type_Workspace = {
    PyObject_HEAD_INIT(NULL)
    .tp_name = "fluggo.media.process.VideoWorkspace",    // tp_name
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_VideoSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) Workspace_dealloc,
    .tp_init = (initproc) Workspace_init,
    .tp_getset = Workspace_getsetters,
    .tp_methods = Workspace_methods,
    .tp_as_sequence = &Workspace_sequence
};

void init_VideoWorkspace( PyObject *module ) {
    py_type_Workspace.tp_basicsize = py_type_VideoSource.tp_basicsize + sizeof(Workspace_private);

    if( PyType_Ready( &py_type_Workspace ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_Workspace );
    PyModule_AddObject( module, "VideoWorkspace", (PyObject *) &py_type_Workspace );

    pysource_funcs = PyCObject_FromVoidPtr( &source_funcs, NULL );

    if( PyType_Ready( &py_type_WorkspaceItem ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_WorkspaceItem );
}


