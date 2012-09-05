/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2010-2 Brian J. Crowell <brian@fluggo.com>

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

    video_source *source;
} py_obj_MPEG2SubsampleFilter;

static int
MPEG2SubsampleFilter_init( py_obj_MPEG2SubsampleFilter *self, PyObject *args, PyObject *kw ) {
    // Zero all pointers (so we know later what needs deleting)
    PyObject *source_obj;

    static char *kwlist[] = { "source", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "O", kwlist, &source_obj ) )
        return -1;

    if( !py_video_take_source( source_obj, &self->source ) )
        return -1;

    return 0;
}

static void
MPEG2SubsampleFilter_dealloc( py_obj_MPEG2SubsampleFilter *self ) {
    py_video_take_source( NULL, &self->source );
    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static coded_image *
MPEG2SubsampleFilter_get_frame( py_obj_MPEG2SubsampleFilter *self, int frame ) {
    rgba_frame_gl temp_frame = { .texture = 0, .full_window = { { 0, 0 }, { 719, 479 } } };

    video_get_frame_gl( self->source, frame, &temp_frame );
    coded_image *result = video_subsample_mpeg2_gl( &temp_frame );

    glDeleteTextures( 1, &temp_frame.texture );

    return result;
}

static coded_image_source_funcs source_funcs = {
    .getFrame = (coded_image_getFrameFunc) MPEG2SubsampleFilter_get_frame,
};

static PyObject *pySourceFuncs;

static PyObject *
MPEG2SubsampleFilter_getFuncs( py_obj_MPEG2SubsampleFilter *self, void *closure ) {
    Py_INCREF(pySourceFuncs);
    return pySourceFuncs;
}

static PyGetSetDef MPEG2SubsampleFilter_getsetters[] = {
    { CODED_IMAGE_SOURCE_FUNCS, (getter) MPEG2SubsampleFilter_getFuncs, NULL, "Coded image source C API." },
    { NULL }
};

static PyMethodDef MPEG2SubsampleFilter_methods[] = {
    { NULL }
};

static PyTypeObject py_type_MPEG2SubsampleFilter = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.MPEG2SubsampleFilter",
    .tp_basicsize = sizeof(py_obj_MPEG2SubsampleFilter),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_CodedImageSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) MPEG2SubsampleFilter_dealloc,
    .tp_init = (initproc) MPEG2SubsampleFilter_init,
    .tp_getset = MPEG2SubsampleFilter_getsetters,
    .tp_methods = MPEG2SubsampleFilter_methods
};

void init_MPEG2SubsampleFilter( PyObject *module ) {
    if( PyType_Ready( &py_type_MPEG2SubsampleFilter ) < 0 )
        return;

    Py_INCREF( &py_type_MPEG2SubsampleFilter );
    PyModule_AddObject( module, "MPEG2SubsampleFilter", (PyObject *) &py_type_MPEG2SubsampleFilter );

    pySourceFuncs = PyCapsule_New( &source_funcs, CODED_IMAGE_SOURCE_FUNCS, NULL );
}



