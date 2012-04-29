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
static GQuark q_interlaceShader;

typedef struct {
    PyObject_HEAD

    video_source *source;
    int offset;
} py_obj_Pulldown23RemovalFilter;

static int
Pulldown23RemovalFilter_init( py_obj_Pulldown23RemovalFilter *self, PyObject *args, PyObject *kwds ) {
    PyObject *source;

    if( !PyArg_ParseTuple( args, "Oi", &source, &self->offset ) )
        return -1;

    if( !py_video_take_source( source, &self->source ) )
        return -1;

    return 0;
}

static void
Pulldown23RemovalFilter_getFrame( py_obj_Pulldown23RemovalFilter *self, int frameIndex, rgba_frame_f16 *frame ) {
    if( self->source == NULL ) {
        // No result
        box2i_set_empty( &frame->current_window );
        return;
    }

    // Cadence offsets:

    // 0 AA BB BC CD DD (0->0, 1->1, 3->4), (2->2b3a)
    // 1 BB BC CD DD EE (0->0, 2->3, 3->4), (1->1b2a)
    // 2 BC CD DD EE FF (1->2, 2->3, 3->4), (0->0b1a)
    // 3 CD DD EE FF FG (0->1, 1->2, 2->3), (3->4b5a) (same as 4 with 1st frame discarded)
    // 4 DD EE FF FG GH (0->0, 1->1, 2->2), (3->3b4a)

    int frameOffset;

    if( self->offset == 4 )
        frameOffset = (frameIndex + 3) & 3;
    else
        frameOffset = (frameIndex + self->offset) & 3;

    int64_t baseFrame = ((frameIndex + self->offset) >> 2) * 5 - self->offset;

    // Solid frames
    if( frameOffset == 0 ) {
        video_get_frame_f16( self->source, baseFrame, frame );
    }
    else if( frameOffset == 1 ) {
        video_get_frame_f16( self->source, baseFrame + 1, frame );
    }
    else if( frameOffset == 3 ) {
        video_get_frame_f16( self->source, baseFrame + 4, frame );
    }
    else {
        // Mixed fields; we want the odds (field #2) from this frame:
        video_get_frame_f16( self->source, baseFrame + 2, frame );

        int height = frame->current_window.max.y - frame->current_window.min.y + 1;
        int width = frame->current_window.max.x - frame->current_window.min.x + 1;

        // We want the evens (field #1) from this next frame
        // TODO: Cache this temp frame between calls
        rgba_frame_f16 tempFrame;
        tempFrame.data = g_slice_alloc( sizeof(rgba_f16) * height * width );
        tempFrame.full_window = frame->current_window;
        tempFrame.current_window = frame->current_window;

        video_get_frame_f16( self->source, baseFrame + 3, &tempFrame );

        for( int i = ((frame->current_window.min.y + 1) & ~1); i <= frame->current_window.max.y; i += 2 ) {
            memcpy( video_get_pixel_f16( frame, frame->current_window.min.x, i ),
                video_get_pixel_f16( &tempFrame, 0, i ),
                width * sizeof(rgba_f16) );
        }

        g_slice_free1( sizeof(rgba_f16) * height * width, tempFrame.data );
    }
}

static const char *interlaceText =
"#version 110\n"
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect texA;"
""
"void main() {"
"    if( cos(gl_TexCoord[0].t * 3.14156) < 0.0 )"
// BJC: I have no clue why either of these variants does not work here:
//"    if( int(gl_FragCoord.t) & 1 == 1 )"
//"    if( int(gl_FragCoord.t) % 2 == 1 )"
"        discard;"

"    gl_FragColor = texture2DRect( texA, gl_FragCoord.st );"
//"    gl_FragColor = vec4(fract(gl_FragCoord.t), 0.0, 0.0, 1.0);"
"}";

typedef struct {
    GLhandleARB shader, program;
} gl_shader_state;

static void destroyShader( gl_shader_state *shader ) {
    // We assume that we're in the right GL context
    glDeleteObjectARB( shader->program );
    glDeleteObjectARB( shader->shader );
}

static void
Pulldown23RemovalFilter_getFrameGL( py_obj_Pulldown23RemovalFilter *self, int frameIndex, rgba_frame_gl *frame ) {
    if( self->source == NULL ) {
        // No result
        box2i_set_empty( &frame->current_window );
        return;
    }

    // Cadence offsets:

    // 0 AA BB BC CD DD (0->0, 1->1, 3->4), (2->2b3a)
    // 1 BB BC CD DD EE (0->0, 2->3, 3->4), (1->1b2a)
    // 2 BC CD DD EE FF (1->2, 2->3, 3->4), (0->0b1a)
    // 3 CD DD EE FF FG (0->1, 1->2, 2->3), (3->4b5a) (same as 4 with 1st frame discarded)
    // 4 DD EE FF FG GH (0->0, 1->1, 2->2), (3->3b4a)

    int frameOffset;

    if( self->offset == 4 )
        frameOffset = (frameIndex + 3) & 3;
    else
        frameOffset = (frameIndex + self->offset) & 3;

    int64_t baseFrame = ((frameIndex + self->offset) >> 2) * 5 - self->offset;

    // Solid frames
    if( frameOffset == 0 ) {
        video_get_frame_gl( self->source, baseFrame, frame );
    }
    else if( frameOffset == 1 ) {
        video_get_frame_gl( self->source, baseFrame + 1, frame );
    }
    else if( frameOffset == 3 ) {
        video_get_frame_gl( self->source, baseFrame + 4, frame );
    }
    else {
        v2i frameSize;
        box2i_get_size( &frame->full_window, &frameSize );

        void *context = getCurrentGLContext();
        gl_shader_state *shader = (gl_shader_state *) g_dataset_id_get_data( context, q_interlaceShader );

        if( !shader ) {
            // Time to create the program for this context
            shader = calloc( sizeof(gl_shader_state), 1 );

            gl_buildShader( interlaceText, &shader->shader, &shader->program );

            g_dataset_id_set_data_full( context, q_interlaceShader, shader, (GDestroyNotify) destroyShader );
        }

        // Mixed fields
        rgba_frame_gl frameB = *frame;

        video_get_frame_gl( self->source, baseFrame + 2, frame );
        video_get_frame_gl( self->source, baseFrame + 3, &frameB );

        glUseProgramObjectARB( shader->program );
        glUniform1iARB( glGetUniformLocationARB( shader->program, "texA" ), 0 );

        glActiveTexture( GL_TEXTURE0 );
        glBindTexture( GL_TEXTURE_RECTANGLE_ARB, frameB.texture );
        glEnable( GL_TEXTURE_RECTANGLE_ARB );

        gl_renderToTexture( frame );

        glUseProgramObjectARB( 0 );
        glDisable( GL_TEXTURE_RECTANGLE_ARB );
        glDeleteTextures( 1, &frameB.texture );
    }
}

static void
Pulldown23RemovalFilter_dealloc( py_obj_Pulldown23RemovalFilter *self ) {
    py_video_take_source( NULL, &self->source );
    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static PyObject *
Pulldown23RemovalFilter_get_new_length( py_obj_Pulldown23RemovalFilter *self, PyObject *args ) {
    int old_length;

    if( !PyArg_ParseTuple( args, "i", &old_length ) )
        return NULL;

    // Cadence offsets:

    // 0 AA BB BC CD DD (0->0, 1->1, 3->4), (2->2b3a)
    // 1 BB BC CD DD EE (0->0, 2->3, 3->4), (1->1b2a)
    // 2 BC CD DD EE FF (1->2, 2->3, 3->4), (0->0b1a)
    // 3 CD DD EE FF FG (0->1, 1->2, 2->3), (3->4b5a) (same as 4 with 1st frame discarded)
    // 4 DD EE FF FG GH (0->0, 1->1, 2->2), (3->3b4a)

    int complete_sequences = old_length / 5;
    int new_length = complete_sequences * 4;

    int partial = old_length % 5;

    // Only one frame is useless: BC/FG
    if( self->offset == 0 && partial > 2 )
        partial--;
    else if( self->offset == 1 && partial > 1 )
        partial--;
    else if( self->offset == 2 && partial > 0 )
        partial--;
    else if( self->offset == 3 && partial > 4 )
        partial--;
    else if( self->offset == 4 && partial > 3 )
        partial--;

    return Py_BuildValue( "i", new_length + partial );
}

static video_frame_source_funcs sourceFuncs = {
    .get_frame = (video_get_frame_func) Pulldown23RemovalFilter_getFrame,
    .get_frame_gl = (video_get_frame_gl_func) Pulldown23RemovalFilter_getFrameGL
};

static PyObject *
Pulldown23RemovalFilter_getFuncs( py_obj_Pulldown23RemovalFilter *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef Pulldown23RemovalFilter_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) Pulldown23RemovalFilter_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PyMethodDef Pulldown23RemovalFilter_methods[] = {
    { "get_new_length", (PyCFunction) Pulldown23RemovalFilter_get_new_length, METH_VARARGS,
        "Calculate the new length of the video without pulldown.\n"
        "\n"
        "new_length = filter.get_length(old_length)" },
    { NULL }
};

static PyTypeObject py_type_Pulldown23RemovalFilter = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.process.Pulldown23RemovalFilter",
    .tp_basicsize = sizeof(py_obj_Pulldown23RemovalFilter),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_VideoSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) Pulldown23RemovalFilter_dealloc,
    .tp_init = (initproc) Pulldown23RemovalFilter_init,
    .tp_getset = Pulldown23RemovalFilter_getsetters,
    .tp_methods = Pulldown23RemovalFilter_methods,
};

void init_Pulldown23RemovalFilter( PyObject *module ) {
    if( PyType_Ready( &py_type_Pulldown23RemovalFilter ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_Pulldown23RemovalFilter );
    PyModule_AddObject( module, "Pulldown23RemovalFilter", (PyObject *) &py_type_Pulldown23RemovalFilter );

    pysourceFuncs = PyCapsule_New( &sourceFuncs, VIDEO_FRAME_SOURCE_FUNCS, NULL );

    q_interlaceShader = g_quark_from_static_string( "Pulldown23RemovalFilter::interlaceShader" );
}



