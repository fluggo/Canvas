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

#define PRIV(obj)        ((rgba_frame_f16*)(((void *) obj) + py_type_VideoSource.tp_basicsize))

static void
RgbaFrameF16_getFrame_f16( PyObject *self, int frame_index, rgba_frame_f16 *frame ) {
    video_copy_frame_f16( frame, PRIV(self) );
}

static void
RgbaFrameF16_dealloc( PyObject *self ) {
    PyMem_Free( PRIV(self)->data );
    self->ob_type->tp_free( self );
}

static video_frame_source_funcs source_funcs = {
    .get_frame = (video_get_frame_func) RgbaFrameF16_getFrame_f16,
};

static PyObject *
RgbaFrameF16_get_funcs( PyObject *self, void *closure ) {
    Py_INCREF(pysource_funcs);
    return pysource_funcs;
}

static PyObject *
RgbaFrameF16_get_full_data_window( PyObject *self, void *closure ) {
    return py_make_box2i( &PRIV(self)->full_window );
}

static PyObject *
RgbaFrameF16_get_current_data_window( PyObject *self, void *closure ) {
    return py_make_box2i( &PRIV(self)->current_window );
}

static PyGetSetDef RgbaFrameF16_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) RgbaFrameF16_get_funcs, NULL, "Video frame source C API." },
    { "full_data_window", (getter) RgbaFrameF16_get_full_data_window, NULL, "The full data window for this frame." },
    { "current_data_window", (getter) RgbaFrameF16_get_current_data_window, NULL, "The current (defined) data window for this frame." },
    { NULL }
};

static Py_ssize_t
RgbaFrameF16_size( PyObject *self ) {
    v2i size;
    box2i_getSize( &PRIV(self)->full_window, &size );

    return size.x * size.y;
}

static PyObject *
color_to_python( rgba_f16 *color ) {
    rgba_f32 result;
    half_convert_to_float( &color->r, &result.r, 4 );

    return py_make_rgba_f32( &result );
}

static PyObject *
RgbaFrameF16_get_item( PyObject *self, Py_ssize_t i ) {
    v2i size;
    box2i_getSize( &PRIV(self)->full_window, &size );

    if( i < 0 || i >= (size.x * size.y) ) {
        PyErr_SetString( PyExc_IndexError, "Index was out of range." );
        return NULL;
    }

    return color_to_python( &PRIV(self)->data[i] );
}

static PySequenceMethods RgbaFrameF16_sequence = {
    .sq_length = (lenfunc) RgbaFrameF16_size,
    .sq_item = (ssizeargfunc) RgbaFrameF16_get_item,
};

static PyObject *
RgbaFrameF16_pixel( PyObject *self, PyObject *args ) {
    int x, y;

    if( !PyArg_ParseTuple( args, "ii", &x, &y ) )
        return NULL;

    box2i *window = &PRIV(self)->current_window;

    if( x < window->min.x || x > window->max.x || y < window->min.y || y > window->max.y )
        Py_RETURN_NONE;

    return color_to_python( video_get_pixel_f16( PRIV(self), x, y ) );
}

static PyObject *
RgbaFrameF16_to_argb32_string( PyObject *self, PyObject *args ) {
    if( box2i_isEmpty( &PRIV(self)->current_window ) )
        Py_RETURN_NONE;

    v2i size;
    box2i_getSize( &PRIV(self)->current_window, &size );

    const uint8_t *ramp = video_get_gamma45_ramp();

    ssize_t len = size.x * size.y;
    uint32_t *data = PyMem_Malloc( len * sizeof(uint32_t) );

    if( !data )
        return NULL;

    for( int y = PRIV(self)->current_window.min.y; y <= PRIV(self)->current_window.max.y; y++ ) {
        rgba_f16 *sy = video_get_pixel_f16( PRIV(self), PRIV(self)->current_window.min.x, y );

        for( int x = 0; x < size.x; x++ ) {
            uint8_t a = ramp[sy[x].a];

            data[(y - PRIV(self)->current_window.min.y) * size.x + x] =
                (a << 24) |
                (((ramp[sy[x].r] * a >> 8) & 0xFF) << 16) |
                (((ramp[sy[x].g] * a >> 8) & 0xFF) << 8) |
                ((ramp[sy[x].b] * a >> 8) & 0xFF);
        }
    }

    PyObject *result = PyString_FromStringAndSize( (const char *) data, len * sizeof(uint32_t) );

    PyMem_Free( data );

    return result;
}

static PyMethodDef RgbaFrameF16_methods[] = {
    { "pixel", (PyCFunction) RgbaFrameF16_pixel, METH_VARARGS,
        "Get the pixel at the given coordinates, or None if the pixel isn't defined in this frame." },
    { "to_argb32_string", (PyCFunction) RgbaFrameF16_to_argb32_string, METH_VARARGS,
        "Get the defined window in the image as a string containing premultiplied ARGB values, suitable for use with QImage." },
    { NULL }
};

static PyTypeObject py_type_RgbaFrameF16 = {
    PyObject_HEAD_INIT(NULL)
    .tp_name = "fluggo.media.process.RgbaFrameF16",    // tp_name
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_base = &py_type_VideoSource,
    .tp_dealloc = (destructor) RgbaFrameF16_dealloc,
    .tp_getset = RgbaFrameF16_getsetters,
    .tp_methods = RgbaFrameF16_methods,
    .tp_as_sequence = &RgbaFrameF16_sequence
};

/*
    Function: py_RgbaFrameF16_new
        Creates an RgbaFrameF16 and gets a pointer to its rgba_frame_f16 structure.
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
py_RgbaFrameF16_new( box2i *full_data_window, rgba_frame_f16 **frame ) {
    PyObject *tuple = PyTuple_New( 0 ), *dict = PyDict_New();

    PyObject *result = py_type_RgbaFrameF16.tp_new( &py_type_RgbaFrameF16, tuple, dict );

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

    PRIV(result)->data = PyMem_Malloc( sizeof(rgba_f16) * size.x * size.y );

    if( !PRIV(result)->data ) {
        Py_DECREF(result);
        return PyErr_NoMemory();
    }

    if( frame )
        *frame = PRIV(result);

    return result;
}

PyObject *
py_get_frame_f16( PyObject *self, PyObject *args, PyObject *kw ) {
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

    PyObject *result = py_RgbaFrameF16_new( &window, NULL );

    if( !result )
        return NULL;

    VideoSourceHolder source = { .csource = NULL };

    if( !py_video_take_source( self, &source ) ) {
        Py_DECREF(result);
        return NULL;
    }

    video_get_frame_f16( &source.source, frame_index, PRIV(result) );

    if( !py_video_take_source( NULL, &source ) ) {
        Py_DECREF(result);
        return NULL;
    }

    return result;
}

void init_RgbaFrameF16( PyObject *module ) {
    py_type_RgbaFrameF16.tp_basicsize = py_type_VideoSource.tp_basicsize + sizeof(rgba_frame_f16);

    if( PyType_Ready( &py_type_RgbaFrameF16 ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_RgbaFrameF16 );
    PyModule_AddObject( module, "RgbaFrameF16", (PyObject *) &py_type_RgbaFrameF16 );

    pysource_funcs = PyCObject_FromVoidPtr( &source_funcs, NULL );
}


