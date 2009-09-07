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

#include "framework.h"

#define FOREACH_PIXEL_BEGIN(frame,pixel) \
    for( int _py = (frame)->currentDataWindow.min.y - (frame)->fullDataWindow.min.y; \
            _py <= (frame)->currentDataWindow.max.y - (frame)->fullDataWindow.min.y; _py++ ) { \
        for( int _px = (frame)->currentDataWindow.min.x - (frame)->fullDataWindow.min.x; \
            _px <= (frame)->currentDataWindow.max.x - (frame)->fullDataWindow.min.x; _px++ ) { \
            rgba_f32 *pixel = &(frame)->frameData[_py * (frame)->stride + _px]; \

#define FOREACH_PIXEL_END } }

#define frame_pixel(frame, vx, vy)    (frame)->frameData[(frame)->stride * ((vy) - (frame)->fullDataWindow.min.y) + ((vx) - (frame)->fullDataWindow.min.x)]

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
    static char *kwlist[] = { "srcA", "srcB", "mixB", NULL };
    PyObject *srcA, *srcB, *mixB;

    if( !PyArg_ParseTupleAndKeywords( args, kwds, "OOO", kwlist,
        &srcA, &srcB, &mixB ) )
        return -1;

    if( !takeVideoSource( srcA, &self->srcA ) )
        return -1;

    if( !takeVideoSource( srcB, &self->srcB ) )
        return -1;

    if( !takeFrameFunc( mixB, &self->mixB ) )
        return -1;

    self->mode = MIXMODE_CROSSFADE;

    return 0;
}

void mulalpha( rgba_f32_frame *frame, float f ) {
    FOREACH_PIXEL_BEGIN(frame, pixel)
        pixel->a *= f;
    FOREACH_PIXEL_END
}

void expand_frame( rgba_f32_frame *frame, box2i newWindow ) {
    int leftClear = frame->currentDataWindow.min.x - newWindow.min.x,
        rightClear = newWindow.max.x - frame->currentDataWindow.max.x;

    // Zero top rows
    for( int y = newWindow.min.y; y < frame->currentDataWindow.min.y; y++ ) {
        memset( &frame_pixel(frame, newWindow.min.x, y), 0,
            sizeof(rgba_f32) * (newWindow.max.x - newWindow.min.x + 1) );
    }

    // Zero sides
    if( leftClear > 0 || rightClear > 0 ) {
        for( int y = frame->currentDataWindow.min.y; y <= frame->currentDataWindow.max.y; y++ ) {
            if( leftClear > 0 )
                memset( &frame_pixel(frame, newWindow.min.x, y), 0,
                    sizeof(rgba_f32) * leftClear );

            if( rightClear > 0 )
                memset( &frame_pixel(frame, frame->currentDataWindow.max.x + 1, y), 0,
                    sizeof(rgba_f32) * rightClear );
        }
    }

    // Zero bottom rows
    for( int y = frame->currentDataWindow.max.y + 1; y <= newWindow.max.y; y++ ) {
        memset( &frame_pixel(frame, newWindow.min.x, y), 0,
            sizeof(rgba_f32) * (newWindow.max.x - newWindow.min.x + 1) );
    }

    // Set the frame size
    frame->currentDataWindow = newWindow;
}

static void
VideoMixFilter_getFrame32( py_obj_VideoMixFilter *self, int frameIndex, rgba_f32_frame *frame ) {
    // Gather the mix factor
    float mixB = self->mixB.constant;

    if( self->mixB.funcs ) {
        long index = frameIndex;

        self->mixB.funcs->getValues( self->mixB.source,
            1, &index, 1, &mixB );
    }

    mixB = clampf(mixB, 0.0f, 1.0f);

    if( self->mode == MIXMODE_CROSSFADE && mixB == 1.0f ) {
        // We only need frame B
        getFrame_f32( &self->srcB, frameIndex, frame );
        return;
    }

    // Gather base frame
    getFrame_f32( &self->srcA, frameIndex, frame );

    // Shortcut out if we can
    if( mixB == 0.0f )
        return;

    rgba_f32_frame tempFrame;
    v2i sizeB;

    switch( self->mode ) {
        case MIXMODE_ADD:
            // These modes don't need all of frame B
            box2i_getSize( &frame->currentDataWindow, &sizeB );
            tempFrame.fullDataWindow = frame->currentDataWindow;
            tempFrame.currentDataWindow = frame->currentDataWindow;
            break;

        case MIXMODE_BLEND:
        case MIXMODE_CROSSFADE:
            // These modes need all of frame B
            box2i_getSize( &frame->fullDataWindow, &sizeB );
            tempFrame.fullDataWindow = frame->fullDataWindow;
            tempFrame.currentDataWindow = frame->fullDataWindow;
            break;

        default:
            // Yeah... dunno.
            return;
    }

    tempFrame.frameData = slice_alloc( sizeof(rgba_f32) * sizeB.y * sizeB.x );
    tempFrame.stride = sizeB.x;

    getFrame_f32( &self->srcB, frameIndex, &tempFrame );

    // Expand them until they're the same size
    box2i newWindow = {
        {    min(frame->currentDataWindow.min.x, tempFrame.currentDataWindow.min.x),
            min(frame->currentDataWindow.min.y, tempFrame.currentDataWindow.min.y) },
        {    max(frame->currentDataWindow.max.x, tempFrame.currentDataWindow.max.x),
            max(frame->currentDataWindow.max.y, tempFrame.currentDataWindow.max.y) } };

    //expand_frame( frame, newWindow );
    //expand_frame( &tempFrame, newWindow );
    int newWidth = newWindow.max.x - newWindow.min.x + 1;

    // Perform the operation
    for( int y = newWindow.min.y; y <= newWindow.max.y; y++ ) {
        rgba_f32 *rowA = &frame_pixel(frame, newWindow.min.x, y);
        rgba_f32 *rowB = &frame_pixel(&tempFrame, newWindow.min.x, y);

        switch( self->mode ) {
            case MIXMODE_ADD:
                for( int x = 0; x < newWidth; x++ ) {
                    rowA[x].r += rowB[x].r * rowB[x].a * mixB;
                    rowA[x].g += rowB[x].g * rowB[x].a * mixB;
                    rowA[x].b += rowB[x].b * rowB[x].a * mixB;
                }
                break;

            case MIXMODE_BLEND:
                for( int x = 0; x < newWidth; x++ ) {
                    // FIXME: Gimp does something different here when
                    // the alpha of the lower layer is < 1.0f
                    rowA[x].r = rowA[x].r * (1.0f - rowB[x].a * mixB)
                        + rowB[x].r * rowB[x].a * mixB;
                    rowA[x].g = rowA[x].g * (1.0f - rowB[x].a * mixB)
                        + rowB[x].g * rowB[x].a * mixB;
                    rowA[x].b = rowA[x].b * (1.0f - rowB[x].a * mixB)
                        + rowB[x].b * rowB[x].a * mixB;
                    rowA[x].a = rowA[x].a + rowB[x].a * (1.0f - rowA[x].a) * mixB;
                }
                break;

            case MIXMODE_CROSSFADE:
                for( int x = 0; x < newWidth; x++ ) {
                    rowA[x].r = rowA[x].r * (1.0f - mixB) + rowB[x].r * mixB;
                    rowA[x].g = rowA[x].g * (1.0f - mixB) + rowB[x].g * mixB;
                    rowA[x].b = rowA[x].b * (1.0f - mixB) + rowB[x].b * mixB;
                    rowA[x].a = rowA[x].a * (1.0f - mixB) + rowB[x].a * mixB;
                }
                break;
        }
    }

    slice_free( sizeof(rgba_f32) * sizeB.y * sizeB.x, tempFrame.frameData );
}

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
"    gl_FragColor = colorA * (1.0 - mixB) + colorB * mixB;"
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
VideoMixFilter_getFrameGL( py_obj_VideoMixFilter *self, int frameIndex, rgba_gl_frame *frame ) {
    // Initial logic is the same as in software
    // Gather the mix factor
    float mixB = self->mixB.constant;

    if( self->mixB.funcs ) {
        long index = frameIndex;

        self->mixB.funcs->getValues( self->mixB.source,
            1, &index, 1, &mixB );
    }

    mixB = clampf(mixB, 0.0f, 1.0f);

    if( self->mode == MIXMODE_CROSSFADE && mixB == 1.0f ) {
        // We only need frame B
        getFrame_gl( &self->srcB, frameIndex, frame );
        return;
    }
    else if( mixB == 0.0f ) {
        getFrame_gl( &self->srcA, frameIndex, frame );
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

    rgba_gl_frame frameA = *frame, frameB = *frame;

    getFrame_gl( &self->srcA, frameIndex, &frameA );
    getFrame_gl( &self->srcB, frameIndex, &frameB );

    glUseProgramObjectARB( shader->program );
    glUniform1iARB( shader->texA, 0 );
    glUniform1iARB( shader->texB, 1 );
    glUniform1fARB( shader->mixB, mixB );

    // Now set up the texture to render to
    v2i frameSize;
    box2i_getSize( &frame->fullDataWindow, &frameSize );

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
}

static void
VideoMixFilter_dealloc( py_obj_VideoMixFilter *self ) {
    takeVideoSource( NULL, &self->srcA );
    takeVideoSource( NULL, &self->srcB );
    takeFrameFunc( NULL, &self->mixB );
    self->ob_type->tp_free( (PyObject*) self );
}

static VideoFrameSourceFuncs sourceFuncs = {
    .getFrame32 = (video_getFrame32Func) VideoMixFilter_getFrame32,
    .getFrameGL = (video_getFrameGLFunc) VideoMixFilter_getFrameGL
};

static PyObject *
VideoMixFilter_getFuncs( py_obj_VideoMixFilter *self, void *closure ) {
    Py_INCREF(pysourceFuncs);
    return pysourceFuncs;
}

static PyGetSetDef VideoMixFilter_getsetters[] = {
    { "_videoFrameSourceFuncs", (getter) VideoMixFilter_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PyTypeObject py_type_VideoMixFilter = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.VideoMixFilter",    // tp_name
    sizeof(py_obj_VideoMixFilter),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
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



