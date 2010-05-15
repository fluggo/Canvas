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
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include "filter.h"

static GQuark q_recon411Shader;

typedef struct {
    PyObject_HEAD

    AVFormatContext *context;
    AVCodecContext *codecContext;
    AVCodec *codec;
    int firstVideoStream;
    float colorMatrix[3][3];
    bool allKeyframes;
    int currentVideoFrame;
    GStaticMutex mutex;
} py_obj_FFVideoSource;

typedef struct {
    float cb, cr;
} cbcr_f32;

static half gamma22[65536];

static void FFVideoSource_getFrame( py_obj_FFVideoSource *self, int frameIndex, rgba_frame_f16 *frame );

static int
FFVideoSource_init( py_obj_FFVideoSource *self, PyObject *args, PyObject *kwds ) {
    int error;
    char *filename;

    // Zero all pointers (so we know later what needs deleting)
    self->context = NULL;
    self->codecContext = NULL;
    self->codec = NULL;

    if( !PyArg_ParseTuple( args, "s", &filename ) )
        return -1;

    av_register_all();

    if( (error = av_open_input_file( &self->context, filename, NULL, 0, NULL )) != 0 ) {
        PyErr_Format( PyExc_Exception, "Could not open the file (%s).", strerror( -error ) );
        return -1;
    }

    if( (error = av_find_stream_info( self->context )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Could not find the stream info (%s).", strerror( -error ) );
        return -1;
    }

    for( int i = 0; i < self->context->nb_streams; i++ ) {
        if( self->context->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO ) {
            self->firstVideoStream = i;
            break;
        }
    }

    if( self->firstVideoStream == -1 ) {
        PyErr_SetString( PyExc_Exception, "Could not find a video stream." );
        return -1;
    }

    self->codecContext = self->context->streams[self->firstVideoStream]->codec;
    self->codec = avcodec_find_decoder( self->codecContext->codec_id );

    if( self->codec == NULL ) {
        PyErr_SetString( PyExc_Exception, "Could not find a codec for the stream." );
        return -1;
    }

    if( (error = avcodec_open( self->codecContext, self->codec )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Could not open a codec (%s).", strerror( -error ) );
        return -1;
    }

    // Rec. 601 weights courtesy of http://www.poynton.com/notes/colour_and_gamma/ColorFAQ.html
/*    self->colorMatrix[0][0] = 1.0f / 219.0f;    // Y -> R
    self->colorMatrix[0][1] = 0.0f;    // Pb -> R, and so on
    self->colorMatrix[0][2] = 1.402f / 244.0f;
    self->colorMatrix[1][0] = 1.0f / 219.0f;
    self->colorMatrix[1][1] = -0.344136f / 244.0f;
    self->colorMatrix[1][2] = -0.714136f / 244.0f;
    self->colorMatrix[2][0] = 1.0f / 219.0f;
    self->colorMatrix[2][1] = 1.772f / 244.0f;
    self->colorMatrix[2][2] = 0.0f;*/

    // Naturally, this page disappeared soon after I referenced it, these are from intersil AN9717
    self->colorMatrix[0][0] = 1.0f;
    self->colorMatrix[0][1] = 0.0f;
    self->colorMatrix[0][2] = 1.371f;
    self->colorMatrix[1][0] = 1.0f;
    self->colorMatrix[1][1] = -0.336f;
    self->colorMatrix[1][2] = -0.698f;
    self->colorMatrix[2][0] = 1.0f;
    self->colorMatrix[2][1] = 1.732f;
    self->colorMatrix[2][2] = 0.0f;

    self->currentVideoFrame = 0;

    // Use MLT's keyframe conditions
    self->allKeyframes = !(strcmp( self->codecContext->codec->name, "mjpeg" ) &&
      strcmp( self->codecContext->codec->name, "rawvideo" ) &&
      strcmp( self->codecContext->codec->name, "dvvideo" ));

    g_static_mutex_init( &self->mutex );

    if( !self->allKeyframes ) {
        // Prime the pump for MPEG so we get frame accuracy (otherwise we seem to start a few frames in)
        FFVideoSource_getFrame( self, 0, NULL );
        av_seek_frame( self->context, self->firstVideoStream, 0, AVSEEK_FLAG_BACKWARD );
    }

    return 0;
}

static void
FFVideoSource_dealloc( py_obj_FFVideoSource *self ) {
    if( self->codecContext != NULL ) {
        avcodec_close( self->codecContext );
        self->codecContext = NULL;
    }

    if( self->context != NULL ) {
        av_close_input_file( self->context );
        self->context = NULL;
    }

    g_static_mutex_free( &self->mutex );

    self->ob_type->tp_free( (PyObject*) self );
}

static bool
read_frame( py_obj_FFVideoSource *self, int frameIndex, AVFrame *frame ) {
    //printf( "Requested %ld\n", frameIndex );

    // This formula should be right for most cases, except, of course, when r_frame_rate is wrong
    AVRational *timeBase = &self->context->streams[self->firstVideoStream]->time_base;
    AVRational *frameRate = &self->context->streams[self->firstVideoStream]->r_frame_rate;
    int64_t frameDuration = (timeBase->den * frameRate->den) / (timeBase->num * frameRate->num);
    int64_t timestamp = frameIndex * (timeBase->den * frameRate->den) / (timeBase->num * frameRate->num) + frameDuration / 2;

    //printf( "frameRate: %d/%d\n", frameRate->num, frameRate->den );
    //printf( "frameDuration: %ld\n", frameDuration );

    if( (uint64_t) self->context->start_time != AV_NOPTS_VALUE )
        timestamp += self->context->start_time;

    if( self->allKeyframes ) {
        if( av_seek_frame( self->context, self->firstVideoStream, frameIndex,
                AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD ) < 0 )
            printf( "Could not seek to frame %d.\n", frameIndex );

        self->currentVideoFrame = frameIndex;
    }
    else {
        // Only bother seeking if we're way off (or it's behind us)
        if( frameIndex < self->currentVideoFrame || (frameIndex - self->currentVideoFrame) >= 15 ) {

            //printf( "Seeking back to %ld...\n", timestamp );
            int seekStamp = timestamp - frameDuration * 3;

            if( seekStamp < 0 )
                seekStamp = 0;

            av_seek_frame( self->context, self->firstVideoStream, seekStamp, AVSEEK_FLAG_BACKWARD );
        }

        self->currentVideoFrame = frameIndex;
    }

    AVPacket packet;
    av_init_packet( &packet );

    // TODO: This won't work for any format in which the packets
    // are not in presentation order
    for( ;; ) {
        //printf( "Reading frame\n" );
        if( av_read_frame( self->context, &packet ) < 0 ) {
            return false;
        }

        if( packet.stream_index != self->firstVideoStream ) {
            //printf( "Not the right stream\n" );
            continue;
        }

        int gotPicture;

        //printf( "Decoding video\n" );
        avcodec_decode_video( self->codecContext, frame, &gotPicture,
            packet.data, packet.size );

        if( !gotPicture ) {
            //printf( "Didn't get a picture\n" );
            continue;
        }

        if( (packet.dts + frameDuration) < timestamp ) {
            //printf( "Too early (%ld vs %ld)\n", packet.dts, timestamp );
            continue;
        }

        //printf( "We'll take that\n" );
        av_free_packet( &packet );
        return true;
    }
}

static void
FFVideoSource_getFrame( py_obj_FFVideoSource *self, int frameIndex, rgba_frame_f16 *frame ) {
    if( frameIndex < 0 ) {
        // No result
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

    // Allocate data structures for reading
    AVFrame avFrame;
    avcodec_get_frame_defaults( &avFrame );

    g_static_mutex_lock( &self->mutex );

    if( !read_frame( self, frameIndex, &avFrame ) ) {
        g_print( "Could not read the frame.\n" );

        if( frame )
            box2i_setEmpty( &frame->currentDataWindow );
    }

    if( !frame ) {
        g_static_mutex_unlock( &self->mutex );
        return;
    }

    // Offset the frame so that line zero is part of the first field
    v2i picOffset = { 0, 0 };

    if( avFrame.interlaced_frame && !avFrame.top_field_first )
        picOffset.y = -1;

    //printf( "pix_fmt: %d\n", self->codecContext->pix_fmt );

    // Dupe the input planes, because FFmpeg will reuse them
    uint8_t *yplane, *cbplane, *crplane;
    uint8_t *yrow, *cbrow, *crrow;

    size_t linesize[3] = { avFrame.linesize[0], avFrame.linesize[1], avFrame.linesize[2] };

    yplane = g_slice_copy( linesize[0] * self->codecContext->height, avFrame.data[0] );
    cbplane = g_slice_copy( linesize[1] * self->codecContext->height, avFrame.data[1] );
    crplane = g_slice_copy( linesize[2] * self->codecContext->height, avFrame.data[2] );

    g_static_mutex_unlock( &self->mutex );

    // Set up the current window
    box2i_set( &frame->currentDataWindow,
        max( picOffset.x, frame->fullDataWindow.min.x ),
        max( picOffset.y, frame->fullDataWindow.min.y ),
        min( self->codecContext->width + picOffset.x - 1, frame->fullDataWindow.max.x ),
        min( self->codecContext->height + picOffset.y - 1, frame->fullDataWindow.max.y ) );

    // Set up subsample support
    int subX;
    float subOffsetX;

    switch( self->codecContext->pix_fmt ) {
        case PIX_FMT_YUV411P:
            subX = 4;
            subOffsetX = 0.0f;
            break;

        case PIX_FMT_YUV422P:
            subX = 2;
            subOffsetX = 0.0f;
            break;

        case PIX_FMT_YUV444P:
            subX = 1;
            subOffsetX = 0.0f;
            break;

        default:
            // TEMP: Wimp out if we don't know the format
            box2i_setEmpty( &frame->currentDataWindow );
            return;
    }

    // BJC: What follows is the horizontal-subsample-only case
    fir_filter triangleFilter = { NULL };
    filter_createTriangle( subX, subOffsetX, &triangleFilter );

    // Temp rows aligned to the AVFrame buffer [0, width)
    rgba_f32 *tempRow = g_slice_alloc( sizeof(rgba_f32) * self->codecContext->width );
    cbcr_f32 *tempChroma = g_slice_alloc( sizeof(cbcr_f32) * self->codecContext->width );

    // Turn into half RGB
    for( int row = frame->currentDataWindow.min.y - picOffset.y; row <= frame->currentDataWindow.max.y - picOffset.y; row++ ) {
        yrow = yplane + (row * linesize[0]);
        cbrow = cbplane + (row * linesize[1]);
        crrow = crplane + (row * linesize[2]);

        memset( tempChroma, 0, sizeof(cbcr_f32) * self->codecContext->width );

        int startx = 0, endx = (self->codecContext->width - 1) / subX;

        for( int x = startx; x <= endx; x++ ) {
            float cb = cbrow[x] - 128.0f, cr = crrow[x] - 128.0f;

            for( int i = max(frame->currentDataWindow.min.x - picOffset.x, x * subX - triangleFilter.center );
                    i <= min(frame->currentDataWindow.max.x - picOffset.x, x * subX + (triangleFilter.width - triangleFilter.center - 1)); i++ ) {

                tempChroma[i].cb += cb * triangleFilter.coeff[i - x * subX + triangleFilter.center];
                tempChroma[i].cr += cr * triangleFilter.coeff[i - x * subX + triangleFilter.center];
            }
        }

        for( int x = frame->currentDataWindow.min.x; x <= frame->currentDataWindow.max.x; x++ ) {
            float y = yrow[x - picOffset.x] - 16.0f;

            tempRow[x].r = y * self->colorMatrix[0][0] +
                tempChroma[x - picOffset.x].cb * self->colorMatrix[0][1] +
                tempChroma[x - picOffset.x].cr * self->colorMatrix[0][2];
            tempRow[x].g = y * self->colorMatrix[1][0] +
                tempChroma[x - picOffset.x].cb * self->colorMatrix[1][1] +
                tempChroma[x - picOffset.x].cr * self->colorMatrix[1][2];
            tempRow[x].b = y * self->colorMatrix[2][0] +
                tempChroma[x - picOffset.x].cb * self->colorMatrix[2][1] +
                tempChroma[x - picOffset.x].cr * self->colorMatrix[2][2];
            tempRow[x].a = 255.0f;
        }

        half *out = &frame->frameData[
            (row + picOffset.y - frame->fullDataWindow.min.y) * frame->stride +
            frame->currentDataWindow.min.x - frame->fullDataWindow.min.x].r;

        half_convert_from_float( (float*)(tempRow + frame->currentDataWindow.min.x - picOffset.x), out,
            (sizeof(rgba_f16) / sizeof(half)) * (frame->currentDataWindow.max.x - frame->currentDataWindow.min.x + 1) );
        half_lookup( gamma22, out, out,
            (sizeof(rgba_f16) / sizeof(half)) * (frame->currentDataWindow.max.x - frame->currentDataWindow.min.x + 1) );
    }

    filter_free( &triangleFilter );
    g_slice_free1( sizeof(rgba_f32) * self->codecContext->width, tempRow );
    g_slice_free1( sizeof(cbcr_f32) * self->codecContext->width, tempChroma );
    g_slice_free1( linesize[0] * self->codecContext->height, yplane );
    g_slice_free1( linesize[1] * self->codecContext->height, cbplane );
    g_slice_free1( linesize[2] * self->codecContext->height, crplane );

#if 0
    else if( self->codecContext->pix_fmt == PIX_FMT_YUV420P ) {
        uint8_t *restrict yplane = avFrame.data[0], *restrict cbplane = avFrame.data[1], *restrict crplane = avFrame.data[2];
        rgba_f16 *restrict frameData = frame->frameData;

        int pyi = avFrame.interlaced_frame ? 2 : 1;

        // 4:2:0 interlaced:
        // 0 -> 0, 2; 2 -> 4, 6; 4 -> 8, 10
        // 1 -> 1, 3; 3 -> 5, 7; 5 -> 9, 11

        // 4:2:0 progressive:
        // 0 -> 0, 1; 1 -> 2, 3; 2 -> 4, 5

        const int minsx = coordWindow.min.x >> 1, maxsx = coordWindow.max.x >> 1;

        for( int sy = coordWindow.min.y / 2; sy <= coordWindow.max.y / 2; sy++ ) {
            int py = sy * 2;

            if( avFrame.interlaced_frame && (sy & 1) == 1 )
                py--;

            for( int i = 0; i < 2; i++ ) {
                uint8_t *restrict cbx = cbplane + minsx, *restrict crx = crplane + minsx;

                for( int sx = minsx; sx <= maxsx; sx++ ) {
                    float cb = *cbx++ - 128.0f, cr = *crx++ - 128.0f;

                    float ccr = cb * self->colorMatrix[0][1] + cr * self->colorMatrix[0][2];
                    float ccg = cb * self->colorMatrix[1][1] + cr * self->colorMatrix[1][2];
                    float ccb = cb * self->colorMatrix[2][1] + cr * self->colorMatrix[2][2];

                    if( py < coordWindow.min.y || py > coordWindow.max.y )
                        continue;

                    for( int px = sx * 2; px <= sx * 2 + 1; px++ ) {
                        if( px < coordWindow.min.x || px > coordWindow.max.x )
                            continue;

                        float cy = yplane[avFrame.linesize[0] * (py + pyi * i) + px] - 16.0f;
                        float in[4] = {
                            cy * self->colorMatrix[0][0] + ccr,
                            cy * self->colorMatrix[1][0] + ccg,
                            cy * self->colorMatrix[2][0] + ccb,
                            256.0f
                        };

                        half *out = &frameData[(py + pyi * i) * frame->stride + px].r;

                        half_convert_from_float_fast( in, out, 4 );
                        half_lookup( gamma22, out, out, 4 );
                    }
                }
            }

            cbplane += avFrame.linesize[1];
            crplane += avFrame.linesize[2];
        }
    }
    else {
        box2i_setEmpty( &frame->currentDataWindow );
    }
#endif
}

static const char *recon411Text =
"#version 110\n"
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect texY;"
"uniform sampler2DRect texCb;"
"uniform sampler2DRect texCr;"
"uniform vec2 picOffset;"
"uniform mat3 yuv2rgb;"

"void main() {"
"    vec2 yTexCoord = gl_TexCoord[0].st - picOffset;"
"    vec2 cTexCoord = (yTexCoord - vec2(0.5, 0.5)) * vec2(0.25, 1.0) + vec2(0.5, 0.5);"
"    float y = texture2DRect( texY, yTexCoord ).r - (16.0/256.0);"
"    float cb = texture2DRect( texCb, cTexCoord ).r - 0.5;"
"    float cr = texture2DRect( texCr, cTexCoord ).r - 0.5;"

"    vec3 ycbcr = vec3(y, cb, cr);"

"    gl_FragColor.rgb = pow(ycbcr * yuv2rgb, vec3(2.2, 2.2, 2.2));"
"    gl_FragColor.a = 1.0;"
"}";

typedef struct {
    GLhandleARB shader, program;
    int texY, texCb, texCr, yuv2rgb, picOffset;
} gl_shader_state;

static void destroyShader( gl_shader_state *shader ) {
    // We assume that we're in the right GL context
    glDeleteObjectARB( shader->program );
    glDeleteObjectARB( shader->shader );
}

static void
FFVideoSource_getFrameGL( py_obj_FFVideoSource *self, int frameIndex, rgba_frame_gl *frame ) {
    if( frameIndex < 0 ) {
        // No result
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

    if( self->codecContext->pix_fmt != PIX_FMT_YUV411P ) {
        g_print( "We don't have an implementation for anything but YUV 4:1:1 yet.\n" );
        return;
    }

    // Now set up the texture to render to
    v2i frameSize;
    box2i_getSize( &frame->fullDataWindow, &frameSize );

    void *context = getCurrentGLContext();
    gl_shader_state *shader = (gl_shader_state *) g_dataset_id_get_data( context, q_recon411Shader );

    if( !shader ) {
        // Time to create the program for this context
        shader = calloc( sizeof(gl_shader_state), 1 );

        gl_buildShader( recon411Text, &shader->shader, &shader->program );

        shader->texY = glGetUniformLocationARB( shader->program, "texY" );
        shader->texCb = glGetUniformLocationARB( shader->program, "texCb" );
        shader->texCr = glGetUniformLocationARB( shader->program, "texCr" );
        shader->yuv2rgb = glGetUniformLocationARB( shader->program, "yuv2rgb" );
        shader->picOffset = glGetUniformLocationARB( shader->program, "picOffset" );

        g_dataset_id_set_data_full( context, q_recon411Shader, shader, (GDestroyNotify) destroyShader );
    }

    GLuint textures[4];
    glGenTextures( 4, textures );

    // Set up the result texture
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, textures[3] );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA_FLOAT16_ATI, frameSize.x, frameSize.y, 0,
        GL_RGBA, GL_HALF_FLOAT_ARB, NULL );
    gl_checkError();

    // Allocate data structures for reading
    AVFrame avFrame;
    avcodec_get_frame_defaults( &avFrame );

    g_static_mutex_lock( &self->mutex );
    if( !read_frame( self, frameIndex, &avFrame ) ) {
        g_print( "Could not read the frame.\n" );

        if( frame )
            box2i_setEmpty( &frame->currentDataWindow );
    }

    if( !frame ) {
        g_static_mutex_unlock( &self->mutex );
        return;
    }

    // Offset the frame so that line zero is part of the first field
    v2i picOffset = { 0, 0 };

    if( avFrame.interlaced_frame && !avFrame.top_field_first )
        picOffset.y = -1;

    box2i_set( &frame->currentDataWindow,
        max( picOffset.x, frame->fullDataWindow.min.x ),
        max( picOffset.y, frame->fullDataWindow.min.y ),
        min( self->codecContext->width + picOffset.x - 1, frame->fullDataWindow.max.x ),
        min( self->codecContext->height + picOffset.y - 1, frame->fullDataWindow.max.y ) );

    // Set up the input textures
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, textures[0] );
    glPixelStorei( GL_UNPACK_ROW_LENGTH, avFrame.linesize[0] );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE8, self->codecContext->width, self->codecContext->height, 0,
        GL_LUMINANCE, GL_UNSIGNED_BYTE, avFrame.data[0] );
    glEnable( GL_TEXTURE_RECTANGLE_ARB );

    glActiveTexture( GL_TEXTURE1 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, textures[1] );
    glPixelStorei( GL_UNPACK_ROW_LENGTH, avFrame.linesize[1] );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE8, self->codecContext->width / 4, self->codecContext->height, 0,
        GL_LUMINANCE, GL_UNSIGNED_BYTE, avFrame.data[1] );
    glEnable( GL_TEXTURE_RECTANGLE_ARB );

    glActiveTexture( GL_TEXTURE2 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, textures[2] );
    glPixelStorei( GL_UNPACK_ROW_LENGTH, avFrame.linesize[2] );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE8, self->codecContext->width / 4, self->codecContext->height, 0,
        GL_LUMINANCE, GL_UNSIGNED_BYTE, avFrame.data[2] );
    glEnable( GL_TEXTURE_RECTANGLE_ARB );

    glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
    g_static_mutex_unlock( &self->mutex );

    glUseProgramObjectARB( shader->program );
    glUniform1iARB( shader->texY, 0 );
    glUniform1iARB( shader->texCb, 1 );
    glUniform1iARB( shader->texCr, 2 );
    glUniformMatrix3fvARB( shader->yuv2rgb, 1, false, &self->colorMatrix[0][0] );
    glUniform2fARB( shader->picOffset, picOffset.x, picOffset.y );

    // The troops are ready; define the image
    frame->texture = textures[3];
    gl_renderToTexture( frame );

    glDeleteTextures( 3, textures );

    glUseProgramObjectARB( 0 );

    glActiveTexture( GL_TEXTURE2 );
    glDisable( GL_TEXTURE_RECTANGLE_ARB );
    glActiveTexture( GL_TEXTURE1 );
    glDisable( GL_TEXTURE_RECTANGLE_ARB );
    glActiveTexture( GL_TEXTURE0 );
    glDisable( GL_TEXTURE_RECTANGLE_ARB );
}

static VideoFrameSourceFuncs videoSourceFuncs = {
    .getFrame = (video_getFrameFunc) FFVideoSource_getFrame,
    .getFrameGL = (video_getFrameGLFunc) FFVideoSource_getFrameGL,
};

static PyObject *pyVideoSourceFuncs;

static PyObject *
FFVideoSource_getFuncs( py_obj_FFVideoSource *self, void *closure ) {
    Py_INCREF(pyVideoSourceFuncs);
    return pyVideoSourceFuncs;
}

static PyGetSetDef FFVideoSource_getsetters[] = {
    { VIDEO_FRAME_SOURCE_FUNCS, (getter) FFVideoSource_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PyObject *
FFVideoSource_size( py_obj_FFVideoSource *self ) {
    v2i size = { self->codecContext->width, self->codecContext->height };
    return py_make_v2i( &size );
}

static PyMethodDef FFVideoSource_methods[] = {
    { "size", (PyCFunction) FFVideoSource_size, METH_NOARGS,
        "Gets the frame size for this video." },
    { NULL }
};

static PyTypeObject py_type_FFVideoSource = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.process.FFVideoSource",    // tp_name
    sizeof(py_obj_FFVideoSource),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_VideoSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) FFVideoSource_dealloc,
    .tp_init = (initproc) FFVideoSource_init,
    .tp_getset = FFVideoSource_getsetters,
    .tp_methods = FFVideoSource_methods
};

void init_FFVideoSource( PyObject *module ) {
    float *f = g_malloc( sizeof(float) * 65536 );

    for( int i = 0; i < 65536; i++ )
        gamma22[i] = (uint16_t) i;

    half_convert_to_float( gamma22, f, 65536 );

    for( int i = 0; i < 65536; i++ )
        f[i] = (f[i] < 0.0f) ? 0.0f : powf( f[i] / 255.0f, 2.2f );

    half_convert_from_float( f, gamma22, 65536 );

    g_free( f );

    if( PyType_Ready( &py_type_FFVideoSource ) < 0 )
        return;

    Py_INCREF( &py_type_FFVideoSource );
    PyModule_AddObject( module, "FFVideoSource", (PyObject *) &py_type_FFVideoSource );

    pyVideoSourceFuncs = PyCObject_FromVoidPtr( &videoSourceFuncs, NULL );

    q_recon411Shader = g_quark_from_static_string( "FFVideoSource::recon411Filter" );
}



