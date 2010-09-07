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

typedef struct {
    PyObject_HEAD
    GThreadPool *pool;
} py_obj_VideoPullQueue;

PyObject *py_RgbaFrameF16_new( box2i *full_data_window, rgba_frame_f16 **frame );

/*
    A video_pull_queue runs a thread pool that you can use to load frames
    in the background. This might be useful for, say...I dunno, maybe
    thumbnails on a video track.

    You supply a callback, a video source, and a frame index to
    <video_pull_queue_append>. When the frame is available, a callback is
    raised in the main event loop thread.

    For now, the pull queue will only pull back 16-bit frames.
*/

typedef struct {
    PyObject_HEAD
    PyObject *callback, *user_data, *pyframe, *owner;
    VideoSourceHolder source;
    int frame_index;
    rgba_frame_f16 *frame;
    volatile bool active;
} py_obj_VideoPullQueueItem;

static PyObject *
VideoPullQueueItem_cancel( py_obj_VideoPullQueueItem *self, PyObject *args ) {
    self->active = false;
    Py_RETURN_NONE;
}

static PyMethodDef VideoPullQueueItem_methods[] = {
    { "cancel", (PyCFunction) VideoPullQueueItem_cancel, METH_NOARGS,
        "Cancels the frame grab." },
    { NULL }
};

static PyTypeObject py_type_VideoPullQueueItem = {
    PyObject_HEAD_INIT(NULL)
    .tp_name = "fluggo.media.process.VideoPullQueueItem",
    .tp_basicsize = sizeof(py_obj_VideoPullQueueItem),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_methods = VideoPullQueueItem_methods,
};

static gboolean
_timeout_callback( py_obj_VideoPullQueueItem *item ) {
    // We're back on solid land, but we need to get Python's permission to proceed
    PyGILState_STATE state = PyGILState_Ensure();

    if( item->active ) {
        PyObject *result = PyObject_CallFunction( item->callback, "iOO",
            item->frame_index, item->pyframe, item->user_data );
        Py_DECREF(result);
    }

    // Clean up (do this immediately, because we may inadvertently be holding a ref to ourself)
    Py_CLEAR( item->callback );
    Py_CLEAR( item->user_data );
    Py_CLEAR( item->pyframe );
    Py_CLEAR( item->owner );

    py_video_takeSource( NULL, &item->source );

    Py_DECREF(item);
    PyGILState_Release( state );

    return false;
}

static void
_thread_func( py_obj_VideoPullQueueItem *item, py_obj_VideoPullQueue *self ) {
    if( item->active )
        video_get_frame_f16( &item->source.source, item->frame_index, item->frame );

    g_timeout_add_full( G_PRIORITY_DEFAULT, 0, (GSourceFunc) _timeout_callback, item, NULL );
}

static int
VideoPullQueue_init( py_obj_VideoPullQueue *self, PyObject *args, PyObject *kw ) {
    // We don't take any arguments now, but we probably will in the future
    self->pool = g_thread_pool_new( (GFunc) _thread_func, self, 2, false, NULL );

    return 0;
}

static PyObject *
VideoPullQueue_enqueue( py_obj_VideoPullQueue *self, PyObject *args, PyObject *kw ) {
    PyObject *source_obj, *callback_obj, *user_data_obj, *window_obj;
    int frame_index;
    box2i window;

    static char *kwlist[] = { "source", "frame_index", "window", "callback", "user_data", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "OiOOO", kwlist,
            &source_obj, &frame_index, &window_obj,
            &callback_obj, &user_data_obj ) )
        return NULL;

    if( !py_parse_box2i( window_obj, &window ) )
        return NULL;

    PyObject *tuple = PyTuple_New( 0 ), *dict = PyDict_New();
    py_obj_VideoPullQueueItem *item = (py_obj_VideoPullQueueItem *)
        py_type_VideoPullQueueItem.tp_new( &py_type_VideoPullQueueItem, tuple, dict );

    if( !item ) {
        Py_CLEAR(tuple);
        Py_CLEAR(dict);
        return NULL;
    }

    Py_CLEAR(tuple);
    Py_CLEAR(dict);

    item->pyframe = py_RgbaFrameF16_new( &window, &item->frame );

    if( !item->pyframe ) {
        Py_DECREF( item );
        return NULL;
    }

    if( !py_video_takeSource( source_obj, &item->source ) ) {
        Py_DECREF( item->pyframe );
        Py_DECREF( item );

        return NULL;
    }

    Py_INCREF(callback_obj);
    Py_INCREF(user_data_obj);
    Py_INCREF(self);

    item->callback = callback_obj;
    item->user_data = user_data_obj;
    item->owner = (PyObject *) self;
    item->frame_index = frame_index;
    item->active = true;

    g_thread_pool_push( self->pool, item, NULL );

    // Increment the item's ref one more time; the queue holds a reference
    // and the caller can hold a reference
    Py_INCREF(item);
    return (PyObject *) item;
}

static void
VideoPullQueue_dealloc( py_obj_VideoPullQueue *self ) {
    // There shouldn't be anything on the pool if we're being dealloc'd
    g_thread_pool_free( self->pool, false, true );

    self->ob_type->tp_free( (PyObject*) self );
}

static PyMethodDef VideoPullQueue_methods[] = {
    { "enqueue", (PyCFunction) VideoPullQueue_enqueue, METH_VARARGS | METH_KEYWORDS,
        "Add a frame fetch job to the queue.\n"
        "\n"
        "enqueue(source, frame_index, window, callback, user_data)\n"
        "\n"
        "source - Video source to pull the frame from.\n"
        "frame_index - Frame to pull.\n"
        "window - Tuple describing the data window to pull.\n"
        "callback - Function to call when the frame is ready. The function should have\n"
        "    the signature func(frame_index, frame, user_data).\n"
        "user_data - Object to pass to the callback function." },
    { NULL }
};

static PyTypeObject py_type_VideoPullQueue = {
    PyObject_HEAD_INIT(NULL)
    .tp_name = "fluggo.media.process.VideoPullQueue",
    .tp_basicsize = sizeof(py_obj_VideoPullQueue),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc) VideoPullQueue_init,
    .tp_dealloc = (destructor) VideoPullQueue_dealloc,
    .tp_methods = VideoPullQueue_methods,
};

void init_VideoPullQueue( PyObject *module ) {
    if( PyType_Ready( &py_type_VideoPullQueue ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_VideoPullQueue );
    PyModule_AddObject( module, "VideoPullQueue", (PyObject *) &py_type_VideoPullQueue );

    if( PyType_Ready( &py_type_VideoPullQueueItem ) < 0 )
        return;
}

