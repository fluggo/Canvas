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

static PyTypeObject *py_type_Sequence;
static PyObject *pysource_funcs;
static Py_ssize_t base_basicsize;

#define PRIV(obj)        ((rgba_frame_f32*)(((void *) obj) + base_basicsize))

static void
RgbaFrameF32_getFrame32( PyObject *self, int frame_index, rgba_frame_f32 *frame ) {
    video_copy_frame_alpha_f32( frame, PRIV(self), 1.0f );
}

static void
RgbaFrameF32_dealloc( PyObject *self ) {
    PyMem_Free( PRIV(self)->frameData );
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
    box2i *window = &PRIV(self)->fullDataWindow;
    return Py_BuildValue( "iiii", window->min.x, window->min.y, window->max.x, window->max.y );
}

static PyObject *
RgbaFrameF32_get_current_data_window( PyObject *self, void *closure ) {
    box2i *window = &PRIV(self)->currentDataWindow;
    return Py_BuildValue( "iiii", window->min.x, window->min.y, window->max.x, window->max.y );
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

    box2i_getSize( &PRIV(self)->fullDataWindow, &size );

    return size.x * size.y;
}

static PyObject *
color_to_python( rgba_f32 *color ) {
    return Py_BuildValue( "ffff", color->r, color->g, color->b, color->a );
}

static PyObject *
RgbaFrameF32_get_item( PyObject *self, Py_ssize_t i ) {
    v2i size;
    box2i_getSize( &PRIV(self)->fullDataWindow, &size );

    if( i < 0 || i >= (size.x * size.y) ) {
        PyErr_SetString( PyExc_IndexError, "Index was out of range." );
        return NULL;
    }

    return color_to_python( &PRIV(self)->frameData[i] );
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

    return color_to_python( getPixel_f32( PRIV(self), x, y ) );
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
    .tp_dealloc = (destructor) RgbaFrameF32_dealloc,
    .tp_getset = RgbaFrameF32_getsetters,
    .tp_methods = RgbaFrameF32_methods,
    .tp_as_sequence = &RgbaFrameF32_sequence
};

PyObject *
py_get_frame_f32( PyObject *self, PyObject *args, PyObject *kw ) {
    PyObject *window_tuple = NULL, *source_obj;
    int frame_index;

    static char *kwlist[] = { "source", "frame", "data_window", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "OiO", kwlist,
            &source_obj, &frame_index, &window_tuple ) )
        return NULL;

    PyObject *result = _PyObject_GC_New( &py_type_RgbaFrameF32 );

    if( !PyArg_ParseTuple( window_tuple, "iiii",
        &PRIV(result)->fullDataWindow.min.x,
        &PRIV(result)->fullDataWindow.min.y,
        &PRIV(result)->fullDataWindow.max.x,
        &PRIV(result)->fullDataWindow.max.y ) ) {
        Py_DECREF(result);
        return NULL;
    }

    v2i size;
    box2i_getSize( &PRIV(result)->fullDataWindow, &size );

    PRIV(result)->stride = size.x;
    PRIV(result)->frameData = PyMem_Malloc( sizeof(rgba_f32) * size.x * size.y );

    if( !PRIV(result)->frameData ) {
        Py_DECREF(result);
        return PyErr_NoMemory();
    }

    VideoSourceHolder source = { .csource = NULL };

    if( !py_video_takeSource( source_obj, &source ) ) {
        Py_DECREF(result);
        PyMem_Free( PRIV(result)->frameData );
        return NULL;
    }

    video_getFrame_f32( &source.source, frame_index, PRIV(result) );

    if( !py_video_takeSource( NULL, &source ) ) {
        Py_DECREF(result);
        PyMem_Free( PRIV(result)->frameData );
        return NULL;
    }

    return result;
}

void init_RgbaFrameF32( PyObject *module ) {
    PyObject *collections = PyImport_ImportModule( "collections" );
    py_type_Sequence = (PyTypeObject*) PyObject_GetAttrString( collections, "Sequence" );
    base_basicsize = py_type_Sequence->tp_basicsize;

    Py_CLEAR( collections );

    py_type_RgbaFrameF32.tp_base = py_type_Sequence;
    py_type_RgbaFrameF32.tp_basicsize = py_type_Sequence->tp_basicsize +
        sizeof(rgba_frame_f32);

    if( PyType_Ready( &py_type_RgbaFrameF32 ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_RgbaFrameF32 );
    PyModule_AddObject( module, "RgbaFrameF32", (PyObject *) &py_type_RgbaFrameF32 );

    pysource_funcs = PyCObject_FromVoidPtr( &source_funcs, NULL );
}


