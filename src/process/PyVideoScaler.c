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

#include "framework.h"
#include "video_scale.h"

static PyObject *pysourceFuncs;

typedef struct {
    PyObject_HEAD

    VideoSourceHolder source;
    FrameFunctionHolder target_point, source_point, scale_factors;
} py_obj_VideoScaler;

static int
VideoScaler_init( py_obj_VideoScaler *self, PyObject *args, PyObject *kw ) {
    PyObject *sourceObj, *target_point_obj, *source_point_obj, *scale_factor_obj;

    static char *kwlist[] = { "source", "target_point", "source_point", "scale_factors", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "OOOO", kwlist,
            &sourceObj, &target_point_obj, &source_point_obj, &scale_factor_obj ) )
        return -1;

    if( !takeVideoSource( sourceObj, &self->source ) )
        return -1;

    if( !frameFunc_takeSource( target_point_obj, &self->target_point ) )
        return -1;

    if( !frameFunc_takeSource( source_point_obj, &self->source_point ) )
        return -1;

    if( !frameFunc_takeSource( scale_factor_obj, &self->scale_factors ) )
        return -1;

    return 0;
}

static void
VideoScaler_get_frame_f32( py_obj_VideoScaler *self, int frame_index, rgba_f32_frame *frame ) {
    if( self->source.source.obj == NULL ) {
        // No result
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

    v2i size;
    box2i_getSize( &frame->fullDataWindow, &size );

    v2f source_point, target_point, scale_factors;
    frameFunc_get_v2f( &self->source_point, frame_index, 1, &source_point );
    frameFunc_get_v2f( &self->target_point, frame_index, 1, &target_point );
    frameFunc_get_v2f( &self->scale_factors, frame_index, 1, &scale_factors );

    rgba_f32_frame temp_frame;
    temp_frame.frameData = g_slice_alloc( sizeof(rgba_f32) * size.x * size.y );
    temp_frame.stride = size.x;
    temp_frame.fullDataWindow = frame->fullDataWindow;

    getFrame_f32( &self->source.source, frame_index, &temp_frame );

    video_scale_bilinear_f32( frame, target_point, &temp_frame, source_point, scale_factors );

    g_slice_free1( sizeof(rgba_f32) * size.x * size.y, temp_frame.frameData );
}

static void
VideoScaler_dealloc( py_obj_VideoScaler *self ) {
    takeVideoSource( NULL, &self->source );
    frameFunc_takeSource( NULL, &self->target_point );
    frameFunc_takeSource( NULL, &self->source_point );
    frameFunc_takeSource( NULL, &self->scale_factors );

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

static PyTypeObject py_type_VideoScaler = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.process.VideoScaler",    // tp_name
    sizeof(py_obj_VideoScaler),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) VideoScaler_dealloc,
    .tp_init = (initproc) VideoScaler_init,
    .tp_getset = VideoScaler_getsetters
};

void init_VideoScaler( PyObject *module ) {
    if( PyType_Ready( &py_type_VideoScaler ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_VideoScaler );
    PyModule_AddObject( module, "VideoScaler", (PyObject *) &py_type_VideoScaler );

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );
}



