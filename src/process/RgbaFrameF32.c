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
#include "video_mix.h"

static PyObject *pysource_funcs;

#define PRIV(obj)        ((rgba_frame_f32*)(((void *) obj) + py_type_VideoSource.tp_basicsize))

static void
RgbaFrameF32_getFrame32( PyObject *self, int frame_index, rgba_frame_f32 *frame ) {
    video_copy_frame_alpha_f32( frame, PRIV(self), 1.0f );
}

static void
RgbaFrameF32_dealloc( PyObject *self ) {
    PyMem_Free( PRIV(self)->data );
    self->ob_type->tp_free( self );
}

static VideoFrameSourceFuncs source_funcs = {
    .getFrame32 = (video_getFrame32Func) RgbaFrameF32_getFrame32,
};

static PyObject *
RgbaFrameF32_get_funcs( PyObject *self, void *closure ) {
    Py_INCREF(pysource_funcs);
    return pysource_funcs;
}

static PyObject *
RgbaFrameF32_get_full_data_window( PyObject *self, void *closure ) {
    box2i *window = &PRIV(self)->full_window;
    return py_make_box2i( window );
}

static PyObject *
RgbaFrameF32_get_current_data_window( PyObject *self, void *closure ) {
    box2i *window = &PRIV(self)->currentDataWindow;
    return py_make_box2i( window );
}

static PyGetSetDef RgbaFrameF32_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) RgbaFrameF32_get_funcs, NULL, "Video frame source C API." },
    { "full_data_window", (getter) RgbaFrameF32_get_full_data_window, NULL, "The full data window for this frame." },
    { "current_data_window", (getter) RgbaFrameF32_get_current_data_window, NULL, "The current (defined) data window for this frame." },
    { NULL }
};

static Py_ssize_t
RgbaFrameF32_size( PyObject *self ) {
    v2i size;

    box2i_getSize( &PRIV(self)->full_window, &size );

    return size.x * size.y;
}

static PyObject *
RgbaFrameF32_get_item( PyObject *self, Py_ssize_t i ) {
    v2i size;
    box2i_getSize( &PRIV(self)->full_window, &size );

    if( i < 0 || i >= (size.x * size.y) ) {
        PyErr_SetString( PyExc_IndexError, "Index was out of range." );
        return NULL;
    }

    return py_make_rgba_f32( &PRIV(self)->data[i] );
}

static PySequenceMethods RgbaFrameF32_sequence = {
    .sq_length = (lenfunc) RgbaFrameF32_size,
    .sq_item = (ssizeargfunc) RgbaFrameF32_get_item,
};

static PyObject *
RgbaFrameF32_pixel( PyObject *self, PyObject *args ) {
    int x, y;

    if( !PyArg_ParseTuple( args, "ii", &x, &y ) )
        return NULL;

    box2i *window = &PRIV(self)->currentDataWindow;

    if( x < window->min.x || x > window->max.x || y < window->min.y || y > window->max.y )
        Py_RETURN_NONE;

    return py_make_rgba_f32( getPixel_f32( PRIV(self), x, y ) );
}

static PyMethodDef RgbaFrameF32_methods[] = {
    { "pixel", (PyCFunction) RgbaFrameF32_pixel, METH_VARARGS,
        "Get the pixel at the given coordinates, or None if the pixel isn't defined in this frame." },
    { NULL }
};

static PyTypeObject py_type_RgbaFrameF32 = {
    PyObject_HEAD_INIT(NULL)
    .tp_name = "fluggo.media.process.RgbaFrameF32",    // tp_name
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_VideoSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) RgbaFrameF32_dealloc,
    .tp_getset = RgbaFrameF32_getsetters,
    .tp_methods = RgbaFrameF32_methods,
    .tp_as_sequence = &RgbaFrameF32_sequence
};

/*
    Function: py_RgbaFrameF32_new
        Creates an RgbaFrameF32 and gets a pointer to its rgba_frame_f32 structure.
        Internal use only.

    Parameters:
        full_data_window - The full data window for the frame.
        frame - Optional pointer to a variable to receive the actual frame.
            Everything but the current data window will be set.

    Returns:
        A reference to the new object if successful, or NULL on an error (an
        exception will be set).
*/
PyObject *
py_RgbaFrameF32_new( box2i *full_data_window, rgba_frame_f32 **frame ) {
    PyObject *tuple = PyTuple_New( 0 ), *dict = PyDict_New();

    PyObject *result = py_type_RgbaFrameF32.tp_new( &py_type_RgbaFrameF32, tuple, dict );

    if( !result ) {
        Py_CLEAR(tuple);
        Py_CLEAR(dict);
        return NULL;
    }

    Py_CLEAR(tuple);
    Py_CLEAR(dict);

    PRIV(result)->full_window = *full_data_window;

    v2i size;
    box2i_getSize( &PRIV(result)->full_window, &size );

    PRIV(result)->data = PyMem_Malloc( sizeof(rgba_f32) * size.x * size.y );

    if( !PRIV(result)->data ) {
        Py_DECREF(result);
        return PyErr_NoMemory();
    }

    if( frame )
        *frame = PRIV(result);

    return result;
}

PyObject *
py_get_frame_f32( PyObject *self, PyObject *args, PyObject *kw ) {
    // This function is now actually a member of the VideoSource base class
    PyObject *window_tuple = NULL;
    int frame_index;

    static char *kwlist[] = { "frame_index", "data_window", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "iO", kwlist,
            &frame_index, &window_tuple ) )
        return NULL;

    box2i window;

    if( !py_parse_box2i( window_tuple, &window ) )
        return NULL;

    PyObject *result = py_RgbaFrameF32_new( &window, NULL );

    if( !result )
        return NULL;

    VideoSourceHolder source = { .csource = NULL };

    if( !py_video_takeSource( self, &source ) ) {
        Py_DECREF(result);
        return NULL;
    }

    video_getFrame_f32( &source.source, frame_index, PRIV(result) );

    if( !py_video_takeSource( NULL, &source ) ) {
        Py_DECREF(result);
        return NULL;
    }

    return result;
}

void init_RgbaFrameF32( PyObject *module ) {
    py_type_RgbaFrameF32.tp_basicsize = py_type_VideoSource.tp_basicsize + sizeof(rgba_frame_f32);

    if( PyType_Ready( &py_type_RgbaFrameF32 ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_RgbaFrameF32 );
    PyModule_AddObject( module, "RgbaFrameF32", (PyObject *) &py_type_RgbaFrameF32 );

    pysource_funcs = PyCObject_FromVoidPtr( &source_funcs, NULL );
}


