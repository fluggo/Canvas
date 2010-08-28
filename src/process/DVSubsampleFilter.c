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
#include "video_reconstruct.h"

typedef struct {
    PyObject_HEAD

    VideoSourceHolder source;
} py_obj_DVSubsampleFilter;

static int
DVSubsampleFilter_init( py_obj_DVSubsampleFilter *self, PyObject *args, PyObject *kw ) {
    // Zero all pointers (so we know later what needs deleting)
    PyObject *source_obj;

    static char *kwlist[] = { "source", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "O", kwlist, &source_obj ) )
        return -1;

    if( !py_video_takeSource( source_obj, &self->source ) )
        return -1;

    return 0;
}

static void
DVSubsampleFilter_dealloc( py_obj_DVSubsampleFilter *self ) {
    py_video_takeSource( NULL, &self->source );
    self->ob_type->tp_free( (PyObject*) self );
}

static coded_image *
DVSubsampleFilter_get_frame( py_obj_DVSubsampleFilter *self, int frame ) {
    rgba_frame_f16 temp_frame;
    const v2i size = { 720, 480 };
    const box2i window = { { 0, -1 }, { 719, 478 } };

    temp_frame.data = g_slice_alloc( sizeof(rgba_f16) * size.y * size.x );
    temp_frame.fullDataWindow = window;
    temp_frame.stride = size.x;

    video_getFrame_f16( &self->source.source, frame, &temp_frame );
    coded_image *result = video_subsample_dv( &temp_frame );

    g_slice_free1( sizeof(rgba_f16) * size.y * size.x, temp_frame.data );

    return result;
}

static coded_image_source_funcs source_funcs = {
    .getFrame = (coded_image_getFrameFunc) DVSubsampleFilter_get_frame,
};

static PyObject *pySourceFuncs;

static PyObject *
DVSubsampleFilter_getFuncs( py_obj_DVSubsampleFilter *self, void *closure ) {
    Py_INCREF(pySourceFuncs);
    return pySourceFuncs;
}

static PyGetSetDef DVSubsampleFilter_getsetters[] = {
    { CODED_IMAGE_SOURCE_FUNCS, (getter) DVSubsampleFilter_getFuncs, NULL, "Coded image source C API." },
    { NULL }
};

static PyMethodDef DVSubsampleFilter_methods[] = {
    { NULL }
};

static PyTypeObject py_type_DVSubsampleFilter = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.process.DVSubsampleFilter",    // tp_name
    sizeof(py_obj_DVSubsampleFilter),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_CodedImageSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) DVSubsampleFilter_dealloc,
    .tp_init = (initproc) DVSubsampleFilter_init,
    .tp_getset = DVSubsampleFilter_getsetters,
    .tp_methods = DVSubsampleFilter_methods
};

void init_DVSubsampleFilter( PyObject *module ) {
    if( PyType_Ready( &py_type_DVSubsampleFilter ) < 0 )
        return;

    Py_INCREF( &py_type_DVSubsampleFilter );
    PyModule_AddObject( module, "DVSubsampleFilter", (PyObject *) &py_type_DVSubsampleFilter );

    pySourceFuncs = PyCObject_FromVoidPtr( &source_funcs, NULL );
}



