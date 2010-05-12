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

    box2i window;
    rgba_f32 color_f32;
    rgba_f16 color_f16;
} py_obj_SolidColorVideoSource;

static int
SolidColorVideoSource_init( py_obj_SolidColorVideoSource *self, PyObject *args, PyObject *kwds ) {
    box2i window;
    rgba_f32 color;

    box2i_set( &window, INT_MIN, INT_MIN, INT_MAX, INT_MAX );

    if( !PyArg_ParseTuple( args, "(ffff)|(iiii)",
            &color.r, &color.g, &color.b, &color.a,
            &window.min.x, &window.min.y, &window.max.x, &window.max.y ) )
        return -1;

    self->window = window;
    self->color_f32 = color;

    half_convert_from_float( &color.r, &self->color_f16.r, 4 );

    return 0;
}

static void
SolidColorVideoSource_getFrame( py_obj_SolidColorVideoSource *self, int frameIndex, rgba_frame_f16 *frame ) {
    v2i size;

    box2i_intersect( &frame->currentDataWindow, &self->window, &frame->fullDataWindow );
    box2i_getSize( &frame->currentDataWindow, &size );

    if( size.x == 0 || size.y == 0 )
        return;

    // Fill first row
    rgba_f16 *first_row = getPixel_f16( frame, frame->currentDataWindow.min.x, frame->currentDataWindow.min.y );

    for( int x = 0; x < size.x; x++ )
        first_row[x] = self->color_f16;

    // Dupe to the rest
    for( int y = 1; y < size.y; y++ ) {
        memcpy( getPixel_f16( frame, frame->currentDataWindow.min.x, frame->currentDataWindow.min.y + y ),
            first_row, sizeof(rgba_f16) * size.x );
    }
}

static void
SolidColorVideoSource_getFrame32( py_obj_SolidColorVideoSource *self, int frameIndex, rgba_frame_f32 *frame ) {
    v2i size;

    box2i_intersect( &frame->currentDataWindow, &self->window, &frame->fullDataWindow );
    box2i_getSize( &frame->currentDataWindow, &size );

    if( size.x == 0 || size.y == 0 )
        return;

    // Fill first row
    rgba_f32 *first_row = getPixel_f32( frame, frame->currentDataWindow.min.x, frame->currentDataWindow.min.y );

    for( int x = 0; x < size.x; x++ )
        first_row[x] = self->color_f32;

    // Dupe to the rest
    for( int y = 1; y < size.y; y++ ) {
        memcpy( getPixel_f32( frame, frame->currentDataWindow.min.x, frame->currentDataWindow.min.y + y ),
            first_row, sizeof(rgba_f32) * size.x );
    }
}

static void
SolidColorVideoSource_getFrameGL( py_obj_SolidColorVideoSource *self, int frameIndex, rgba_frame_gl *frame ) {
    v2i size, frameSize;

    box2i_intersect( &frame->currentDataWindow, &self->window, &frame->fullDataWindow );
    box2i_getSize( &frame->currentDataWindow, &size );

    if( size.x == 0 || size.y == 0 )
        return;

    box2i_getSize( &frame->fullDataWindow, &frameSize );

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
    glColor4fv( &self->color_f32.r );
    glVertex2i( frame->currentDataWindow.min.x - frame->fullDataWindow.min.x,
        frame->currentDataWindow.min.y - frame->fullDataWindow.min.y );
    glVertex2i( frame->currentDataWindow.max.x - frame->fullDataWindow.min.x + 1,
        frame->currentDataWindow.min.y - frame->fullDataWindow.min.y );
    glVertex2i( frame->currentDataWindow.max.x - frame->fullDataWindow.min.x + 1,
        frame->currentDataWindow.max.y - frame->fullDataWindow.min.y + 1 );
    glVertex2i( frame->currentDataWindow.min.x - frame->fullDataWindow.min.x,
        frame->currentDataWindow.max.y - frame->fullDataWindow.min.y + 1 );
    glEnd();

    glDeleteFramebuffersEXT( 1, &fbo );
}

static void
SolidColorVideoSource_dealloc( py_obj_SolidColorVideoSource *self ) {
    self->ob_type->tp_free( (PyObject*) self );
}

static VideoFrameSourceFuncs sourceFuncs = {
    .getFrame = (video_getFrameFunc) SolidColorVideoSource_getFrame,
    .getFrame32 = (video_getFrame32Func) SolidColorVideoSource_getFrame32,
    .getFrameGL = (video_getFrameGLFunc) SolidColorVideoSource_getFrameGL
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



