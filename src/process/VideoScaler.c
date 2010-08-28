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

static PyObject *pysourceFuncs;

typedef struct {
    PyObject_HEAD

    VideoSourceHolder source;
    FrameFunctionHolder target_point, source_point, scale_factors, source_rect;
} py_obj_VideoScaler;

static int
VideoScaler_init( py_obj_VideoScaler *self, PyObject *args, PyObject *kw ) {
    PyObject *sourceObj, *target_point_obj, *source_point_obj, *scale_factor_obj, *source_rect_obj;

    static char *kwlist[] = { "source", "target_point", "source_point", "scale_factors", "source_rect", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "OOOOO", kwlist,
            &sourceObj, &target_point_obj, &source_point_obj, &scale_factor_obj, &source_rect_obj ) )
        return -1;

    if( !py_video_takeSource( sourceObj, &self->source ) )
        return -1;

    if( !py_frameFunc_takeSource( target_point_obj, &self->target_point ) )
        return -1;

    if( !py_frameFunc_takeSource( source_point_obj, &self->source_point ) )
        return -1;

    if( !py_frameFunc_takeSource( scale_factor_obj, &self->scale_factors ) )
        return -1;

    if( !py_frameFunc_takeSource( source_rect_obj, &self->source_rect ) )
        return -1;

    return 0;
}

static void
VideoScaler_get_frame_f32( py_obj_VideoScaler *self, int frame_index, rgba_frame_f32 *frame ) {
    if( self->source.source.obj == NULL ) {
        // No result
        box2i_setEmpty( &frame->current_window );
        return;
    }

    v2f source_point, target_point, scale_factors;
    box2i source_rect;
    frameFunc_get_v2f( &self->source_point, frame_index, 1, &source_point );
    frameFunc_get_v2f( &self->target_point, frame_index, 1, &target_point );
    frameFunc_get_v2f( &self->scale_factors, frame_index, 1, &scale_factors );
    frameFunc_get_box2i( &self->source_rect, frame_index, 1, &source_rect );

    video_scale_bilinear_f32_pull( frame, target_point, &self->source.source, frame_index, &source_rect, source_point, scale_factors );
}

static void
VideoScaler_dealloc( py_obj_VideoScaler *self ) {
    py_video_takeSource( NULL, &self->source );
    py_frameFunc_takeSource( NULL, &self->target_point );
    py_frameFunc_takeSource( NULL, &self->source_point );
    py_frameFunc_takeSource( NULL, &self->scale_factors );

    self->ob_type->tp_free( (PyObject*) self );
}

static VideoFrameSourceFuncs sourceFuncs = {
    .getFrame32 = (video_getFrame32Func) VideoScaler_get_frame_f32,
};

static PyObject *
VideoScaler_getFuncs( py_obj_VideoScaler *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef VideoScaler_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) VideoScaler_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PyObject *
VideoScaler_source( py_obj_VideoScaler *self, PyObject *dummy ) {
    if( !self->source.source.obj )
        Py_RETURN_NONE;

    Py_INCREF(self->source.source.obj);
    return self->source.source.obj;
}

static PyObject *
VideoScaler_set_source( py_obj_VideoScaler *self, PyObject *pysource ) {
    if( !py_video_takeSource( pysource, &self->source ) )
        return NULL;

    Py_RETURN_NONE;
}

static PyMethodDef VideoScaler_methods[] = {
    { "source", (PyCFunction) VideoScaler_source, METH_NOARGS,
        "Gets the source for the scaler." },
    { "set_source", (PyCFunction) VideoScaler_set_source, METH_O,
        "Sets the source for the scaler." },
    { NULL }
};

static PyTypeObject py_type_VideoScaler = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.process.VideoScaler",    // tp_name
    sizeof(py_obj_VideoScaler),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_VideoSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) VideoScaler_dealloc,
    .tp_init = (initproc) VideoScaler_init,
    .tp_getset = VideoScaler_getsetters,
    .tp_methods = VideoScaler_methods,
};

void init_VideoScaler( PyObject *module ) {
    if( PyType_Ready( &py_type_VideoScaler ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_VideoScaler );
    PyModule_AddObject( module, "VideoScaler", (PyObject *) &py_type_VideoScaler );

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );
}



