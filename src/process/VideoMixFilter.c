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

static GQuark q_crossfadeShader;

typedef enum {
    MIXMODE_BLEND,
    MIXMODE_ADD,
    MIXMODE_CROSSFADE
} MixMode;

static PyObject *pysourceFuncs;

typedef struct {
    PyObject_HEAD

    VideoSourceHolder srcA, srcB;
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

    if( !py_frameFunc_takeSource( mixB, &self->mixB ) )
        return -1;

    self->mode = MIXMODE_CROSSFADE;

    return 0;
}

static void
VideoMixFilter_getFrame32( py_obj_VideoMixFilter *self, int frameIndex, rgba_frame_f32 *frame ) {
    float mixB = frameFunc_get_f32( &self->mixB, frameIndex, 1 );

    video_mix_cross_f32_pull( frame, &self->srcA.source, frameIndex, &self->srcB.source, frameIndex, mixB );
}

// This crossfade is based on the associative alpha blending formula from:
//    http://en.wikipedia.org/w/index.php?title=Alpha_compositing&oldid=337850364

static const char *crossfadeShaderText =
"#version 110\n"
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect texA;"
"uniform sampler2DRect texB;"
"uniform float mixB;"
""
"void main() {"
"    vec4 colorA = texture2DRect( texA, gl_FragCoord.st );"
"    vec4 colorB = texture2DRect( texB, gl_FragCoord.st );"
""
"    float alpha_a = colorA.a * (1.0 - mixB);"
"    float alpha_b = colorB.a * mixB;"
""
"    gl_FragColor.a = alpha_a + alpha_b;"
""
"    if( gl_FragColor.a != 0.0 )"
"        gl_FragColor.rgb = (colorA.rgb * alpha_a + colorB.rgb * alpha_b) / gl_FragColor.a;"
"    else"
"        gl_FragColor.rgb = vec3(0.0, 0.0, 0.0);"
"}";

typedef struct {
    GLhandleARB shader, program;
    int texA, texB, mixB;
} gl_shader_state;

static void destroyShader( gl_shader_state *shader ) {
    // We assume that we're in the right GL context
    glDeleteObjectARB( shader->program );
    glDeleteObjectARB( shader->shader );
}

void checkGLError();

static void
VideoMixFilter_getFrameGL( py_obj_VideoMixFilter *self, int frameIndex, rgba_frame_gl *frame ) {
    // Initial logic is the same as in software
    // Gather the mix factor
    float mixB = frameFunc_get_f32( &self->mixB, frameIndex, 1 );
    mixB = clampf(mixB, 0.0f, 1.0f);

    if( self->mode == MIXMODE_CROSSFADE && mixB == 1.0f ) {
        // We only need frame B
        video_get_frame_gl( &self->srcB.source, frameIndex, frame );
        return;
    }
    else if( mixB == 0.0f ) {
        video_get_frame_gl( &self->srcA.source, frameIndex, frame );
        return;
    }

    void *context = getCurrentGLContext();

    gl_shader_state *shader = (gl_shader_state *) g_dataset_id_get_data( context, q_crossfadeShader );

    if( !shader ) {
        // Time to create the program for this context
        shader = calloc( sizeof(gl_shader_state), 1 );

        gl_buildShader( crossfadeShaderText, &shader->shader, &shader->program );

        shader->texA = glGetUniformLocationARB( shader->program, "texA" );
        shader->texB = glGetUniformLocationARB( shader->program, "texB" );
        shader->mixB = glGetUniformLocationARB( shader->program, "mixB" );

        g_dataset_id_set_data_full( context, q_crossfadeShader, shader, (GDestroyNotify) destroyShader );
    }

    rgba_frame_gl frameA = *frame, frameB = *frame;

    video_get_frame_gl( &self->srcA.source, frameIndex, &frameA );
    video_get_frame_gl( &self->srcB.source, frameIndex, &frameB );

    glUseProgramObjectARB( shader->program );
    glUniform1iARB( shader->texA, 0 );
    glUniform1iARB( shader->texB, 1 );
    glUniform1fARB( shader->mixB, mixB );

    // Now set up the texture to render to
    v2i frameSize;
    box2i_get_size( &frame->full_window, &frameSize );
    box2i_union( &frame->current_window, &frameA.current_window, &frameB.current_window );

    glGenTextures( 1, &frame->texture );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, frame->texture );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA_FLOAT16_ATI, frameSize.x, frameSize.y, 0,
        GL_RGBA, GL_HALF_FLOAT_ARB, NULL );

    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, frameA.texture );
    glEnable( GL_TEXTURE_RECTANGLE_ARB );

    glActiveTexture( GL_TEXTURE1 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, frameB.texture );
    glEnable( GL_TEXTURE_RECTANGLE_ARB );

    gl_renderToTexture( frame );

    glDeleteTextures( 1, &frameA.texture );
    glDeleteTextures( 1, &frameB.texture );

    glUseProgramObjectARB( 0 );
    glDisable( GL_TEXTURE_RECTANGLE_ARB );
    glActiveTexture( GL_TEXTURE0 );
    glDisable( GL_TEXTURE_RECTANGLE_ARB );
}

static void
VideoMixFilter_dealloc( py_obj_VideoMixFilter *self ) {
    py_video_take_source( NULL, &self->srcA );
    py_video_take_source( NULL, &self->srcB );
    py_frameFunc_takeSource( NULL, &self->mixB );
    self->ob_type->tp_free( (PyObject*) self );
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
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.process.VideoMixFilter",    // tp_name
    sizeof(py_obj_VideoMixFilter),    // tp_basicsize
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

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );

    q_crossfadeShader = g_quark_from_static_string( "VideoMixFilter::crossfadeShader" );
}



