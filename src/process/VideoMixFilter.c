/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2009 Brian J. Crowell <brian@fluggo.com>

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

typedef enum {
    MIXMODE_BLEND,
    MIXMODE_ADD,
    MIXMODE_CROSSFADE
} MixMode;

static PyObject *pysourceFuncs;

typedef struct {
    PyObject_HEAD

    video_source *srcA, *srcB;
    FrameFunctionHolder mixB;
    MixMode mode;
} py_obj_VideoMixFilter;

static int
VideoMixFilter_init( py_obj_VideoMixFilter *self, PyObject *args, PyObject *kwds ) {
    static char *kwlist[] = { "src_a", "src_b", "mix_b", NULL };
    PyObject *srcA, *srcB, *mixB;

    if( !PyArg_ParseTupleAndKeywords( args, kwds, "OOO", kwlist,
        &srcA, &srcB, &mixB ) )
        return -1;

    if( !py_video_take_source( srcA, &self->srcA ) )
        return -1;

    if( !py_video_take_source( srcB, &self->srcB ) )
        return -1;

    if( !py_framefunc_take_source( mixB, &self->mixB ) )
        return -1;

    self->mode = MIXMODE_CROSSFADE;

    return 0;
}

static void
VideoMixFilter_getFrame32( py_obj_VideoMixFilter *self, int frameIndex, rgba_frame_f32 *frame ) {
    float mixB = framefunc_get_f32( &self->mixB, frameIndex );

    video_mix_cross_f32_pull( frame, self->srcA, frameIndex, self->srcB, frameIndex, mixB );
}

static void
VideoMixFilter_getFrameGL( py_obj_VideoMixFilter *self, int frameIndex, rgba_frame_gl *frame ) {
    float mix_b = framefunc_get_f32( &self->mixB, frameIndex );

    video_mix_cross_gl_pull( frame, self->srcA, frameIndex, self->srcB, frameIndex, mix_b );
}

static void
VideoMixFilter_dealloc( py_obj_VideoMixFilter *self ) {
    py_video_take_source( NULL, &self->srcA );
    py_video_take_source( NULL, &self->srcB );
    py_framefunc_take_source( NULL, &self->mixB );

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static video_frame_source_funcs sourceFuncs = {
    .get_frame_32 = (video_get_frame_32_func) VideoMixFilter_getFrame32,
    .get_frame_gl = (video_get_frame_gl_func) VideoMixFilter_getFrameGL
};

static PyObject *
VideoMixFilter_getFuncs( py_obj_VideoMixFilter *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef VideoMixFilter_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) VideoMixFilter_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PyTypeObject py_type_VideoMixFilter = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.VideoMixFilter",
    .tp_basicsize = sizeof(py_obj_VideoMixFilter),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_VideoSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) VideoMixFilter_dealloc,
    .tp_init = (initproc) VideoMixFilter_init,
    .tp_getset = VideoMixFilter_getsetters
};

void init_VideoMixFilter( PyObject *module ) {
    if( PyType_Ready( &py_type_VideoMixFilter ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_VideoMixFilter );
    PyModule_AddObject( module, "VideoMixFilter", (PyObject *) &py_type_VideoMixFilter );

    pysourceFuncs = PyCapsule_New( &sourceFuncs, VIDEO_FRAME_SOURCE_FUNCS, NULL );
}



