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

static PyObject *pysourceFuncs;

typedef struct {
    PyObject_HEAD

    FrameFunctionHolder window, color_f32;
} py_obj_SolidColorVideoSource;

static int
SolidColorVideoSource_init( py_obj_SolidColorVideoSource *self, PyObject *args, PyObject *kwds ) {
    PyObject *window_obj = NULL, *color_obj;

    if( !PyArg_ParseTuple( args, "O|O", &color_obj, &window_obj ) )
        return -1;

    if( !py_framefunc_take_source( color_obj, &self->color_f32 ) )
        return -1;

    self->window.constant[0] = INT_MIN;
    self->window.constant[1] = INT_MIN;
    self->window.constant[2] = INT_MAX;
    self->window.constant[3] = INT_MAX;

    if( window_obj && !py_framefunc_take_source( window_obj, &self->window ) )
        return -1;

    return 0;
}

static void
SolidColorVideoSource_getFrame( py_obj_SolidColorVideoSource *self, int frameIndex, rgba_frame_f16 *frame ) {
    box2i window;
    v2i size;

    framefunc_get_box2i( &window, &self->window, frameIndex );

    box2i_intersect( &frame->current_window, &window, &frame->full_window );
    box2i_get_size( &frame->current_window, &size );

    if( size.x == 0 || size.y == 0 )
        return;

    rgba_f32 color_f32;
    rgba_f16 color_f16;

    framefunc_get_rgba_f32( &color_f32, &self->color_f32, frameIndex );
    rgba_f32_to_f16( &color_f16, &color_f32, 1 );

    for( int y = frame->current_window.min.y; y <= frame->current_window.max.y; y++ ) {
        rgba_f16 *row = video_get_pixel_f16( frame, frame->current_window.min.x, y );

        for( int x = 0; x < size.x; x++ )
            row[x] = color_f16;
    }
}

static void
SolidColorVideoSource_getFrame32( py_obj_SolidColorVideoSource *self, int frameIndex, rgba_frame_f32 *frame ) {
    box2i window;
    v2i size;

    framefunc_get_box2i( &window, &self->window, frameIndex );

    box2i_intersect( &frame->current_window, &window, &frame->full_window );
    box2i_get_size( &frame->current_window, &size );

    if( size.x == 0 || size.y == 0 )
        return;

    rgba_f32 color_f32;
    framefunc_get_rgba_f32( &color_f32, &self->color_f32, frameIndex );

    for( int y = frame->current_window.min.y; y <= frame->current_window.max.y; y++ ) {
        rgba_f32 *row = video_get_pixel_f32( frame, frame->current_window.min.x, y );

        for( int x = 0; x < size.x; x++ )
            row[x] = color_f32;
    }
}

static void
SolidColorVideoSource_getFrameGL( py_obj_SolidColorVideoSource *self, int frameIndex, rgba_frame_gl *frame ) {
    box2i window;
    v2i size, frameSize;

    framefunc_get_box2i( &window, &self->window, frameIndex );

    box2i_intersect( &frame->current_window, &window, &frame->full_window );
    box2i_get_size( &frame->current_window, &size );

    if( size.x == 0 || size.y == 0 )
        return;

    box2i_get_size( &frame->full_window, &frameSize );

    rgba_f32 color_f32;
    framefunc_get_rgba_f32( &color_f32, &self->color_f32, frameIndex );

    glGenTextures( 1, &frame->texture );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, frame->texture );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA_FLOAT16_ATI, frameSize.x, frameSize.y, 0,
        GL_RGBA, GL_HALF_FLOAT_ARB, NULL );

    GLuint fbo;
    glGenFramebuffersEXT( 1, &fbo );
    glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, fbo );
    glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB,
        frame->texture, 0 );

    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
    glClear( GL_COLOR_BUFFER_BIT );

    glLoadIdentity();
    glOrtho( 0, frameSize.x, 0, frameSize.y, -1, 1 );
    glViewport( 0, 0, frameSize.x, frameSize.y );

    glBegin( GL_QUADS );
    glColor4fv( &color_f32.r );
    glVertex2i( frame->current_window.min.x - frame->full_window.min.x,
        frame->current_window.min.y - frame->full_window.min.y );
    glVertex2i( frame->current_window.max.x - frame->full_window.min.x + 1,
        frame->current_window.min.y - frame->full_window.min.y );
    glVertex2i( frame->current_window.max.x - frame->full_window.min.x + 1,
        frame->current_window.max.y - frame->full_window.min.y + 1 );
    glVertex2i( frame->current_window.min.x - frame->full_window.min.x,
        frame->current_window.max.y - frame->full_window.min.y + 1 );
    glEnd();

    glDeleteFramebuffersEXT( 1, &fbo );
}

static void
SolidColorVideoSource_dealloc( py_obj_SolidColorVideoSource *self ) {
    py_framefunc_take_source( NULL, &self->window );
    py_framefunc_take_source( NULL, &self->color_f32 );
    self->ob_type->tp_free( (PyObject*) self );
}

static video_frame_source_funcs sourceFuncs = {
    .get_frame = (video_get_frame_func) SolidColorVideoSource_getFrame,
    .get_frame_32 = (video_get_frame_32_func) SolidColorVideoSource_getFrame32,
    .get_frame_gl = (video_get_frame_gl_func) SolidColorVideoSource_getFrameGL
};

static PyObject *
SolidColorVideoSource_getFuncs( py_obj_SolidColorVideoSource *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef SolidColorVideoSource_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) SolidColorVideoSource_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PyTypeObject py_type_SolidColorVideoSource = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.process.SolidColorVideoSource",    // tp_name
    sizeof(py_obj_SolidColorVideoSource),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_VideoSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) SolidColorVideoSource_dealloc,
    .tp_init = (initproc) SolidColorVideoSource_init,
    .tp_getset = SolidColorVideoSource_getsetters,
};

void init_SolidColorVideoSource( PyObject *module ) {
    if( PyType_Ready( &py_type_SolidColorVideoSource ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_SolidColorVideoSource );
    PyModule_AddObject( module, "SolidColorVideoSource", (PyObject *) &py_type_SolidColorVideoSource );

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );
}



