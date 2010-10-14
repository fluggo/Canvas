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

    CodedImageSourceHolder source;
} py_obj_DVReconstructionFilter;

static int
DVReconstructionFilter_init( py_obj_DVReconstructionFilter *self, PyObject *args, PyObject *kw ) {
    // Zero all pointers (so we know later what needs deleting)
    PyObject *source_obj;
    static char *kwlist[] = { "source", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "O", kwlist, &source_obj ) )
        return -1;

    if( !py_coded_image_take_source( source_obj, &self->source ) )
        return -1;

    return 0;
}

static void
DVReconstructionFilter_dealloc( py_obj_DVReconstructionFilter *self ) {
    py_coded_image_take_source( NULL, &self->source );
    self->ob_type->tp_free( (PyObject*) self );
}

static void
DVReconstructionFilter_get_frame( py_obj_DVReconstructionFilter *self, int frame_index, rgba_frame_f16 *frame ) {
    coded_image *image = self->source.source.funcs->getFrame( self->source.source.obj, frame_index );

    if( !image ) {
        box2i_set_empty( &frame->current_window );
        return;
    }

    video_reconstruct_dv( image, frame );

    if( image->free_func )
        image->free_func( image );
}

static video_frame_source_funcs source_funcs = {
    .get_frame = (video_get_frame_func) DVReconstructionFilter_get_frame,
};

static PyObject *pySourceFuncs;

static PyObject *
DVReconstructionFilter_getFuncs( py_obj_DVReconstructionFilter *self, void *closure ) {
    Py_INCREF(pySourceFuncs);
    return pySourceFuncs;
}

static PyGetSetDef DVReconstructionFilter_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) DVReconstructionFilter_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PyTypeObject py_type_DVReconstructionFilter = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.process.DVReconstructionFilter",    // tp_name
    sizeof(py_obj_DVReconstructionFilter),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_VideoSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) DVReconstructionFilter_dealloc,
    .tp_init = (initproc) DVReconstructionFilter_init,
    .tp_getset = DVReconstructionFilter_getsetters,
};

void init_DVReconstructionFilter( PyObject *module ) {
    if( PyType_Ready( &py_type_DVReconstructionFilter ) < 0 )
        return;

    Py_INCREF( &py_type_DVReconstructionFilter );
    PyModule_AddObject( module, "DVReconstructionFilter", (PyObject *) &py_type_DVReconstructionFilter );

    pySourceFuncs = PyCObject_FromVoidPtr( &source_funcs, NULL );
}



