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
#include <libavformat/avformat.h>

typedef struct __tag_my_coded_image {
    coded_image image;
    int ref_count;
} my_coded_image;

static void
my_coded_image_free( my_coded_image *image ) {
    if( g_atomic_int_dec_and_test( &image->ref_count ) ) {
        g_free( image->image.data[0] );
        g_slice_free( my_coded_image, image );
    }
}

static int
counted_get_buffer( AVCodecContext *codec, AVFrame *frame ) {
    // Fill the frame's base, data, and linesize values.
    // Thoroughly depressing, but the FF people don't give us much of a
    // choice. They want to allocate and release the frames when they
    // want to, not when we want to.

    int size = avpicture_get_size( codec->pix_fmt, codec->width, codec->height );
    void *buffer = g_try_malloc( size );

    if( !buffer )
        return -1;

    AVPicture picture;
    avpicture_fill( &picture, buffer, codec->pix_fmt, codec->width, codec->height );

    my_coded_image *image = g_slice_new0( my_coded_image );

    for( int i = 0; i < 4; i++ ) {
        frame->base[i] = picture.data[i];

        frame->data[i] = picture.data[i];
        image->image.data[i] = picture.data[i];

        frame->linesize[i] = picture.linesize[i];
        image->image.stride[i] = picture.linesize[i];
    }

    frame->opaque = image;
    frame->type = FF_BUFFER_TYPE_USER;

    image->ref_count = 1;
    image->image.free_func = (GFreeFunc) my_coded_image_free;
    image->image.line_count[0] = codec->height;

    // Not perfect for chroma_height, but should be a good start
    int chroma_height = codec->height;

    switch( codec->pix_fmt ) {
        case PIX_FMT_YUV420P:
        case PIX_FMT_YUVJ420P:
        case PIX_FMT_YUV440P:
        case PIX_FMT_YUVA420P:
        //case PIX_FMT_YUV420P16LE:
        //case PIX_FMT_YUV420P16BE:
            chroma_height >>= 1;
            break;

        case PIX_FMT_YUV410P:
            chroma_height >>= 2;
            break;

        default:
            break;
    }

    if( image->image.data[1] )
        image->image.line_count[1] = chroma_height;

    if( image->image.data[2] )
        image->image.line_count[2] = chroma_height;

    return 0;
}

static void
counted_release_buffer( AVCodecContext *codec, AVFrame *frame ) {
    my_coded_image_free( (my_coded_image*) frame->opaque );

    frame->data[0] = NULL;
    frame->opaque = NULL;
}

typedef struct {
    PyObject_HEAD

    CodecPacketSourceHolder source;
    AVCodecContext context;
    int64_t next_frame;

    GStaticMutex mutex;
} py_obj_AVVideoDecoder;

static int
AVVideoDecoder_init( py_obj_AVVideoDecoder *self, PyObject *args, PyObject *kw ) {
    int error;

    // Zero all pointers (so we know later what needs deleting)
    PyObject *source_obj;
    const char *codec_name;

    static char *kwlist[] = { "source", "codec", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "Os", kwlist, &source_obj, &codec_name ) )
        return -1;

    avcodec_register_all();
    AVCodec *codec = avcodec_find_decoder_by_name( codec_name );

    if( !codec ) {
        PyErr_Format( PyExc_Exception, "Could not find the codec \"%s\".", codec_name );
        return -1;
    }

    if( !py_codec_packet_take_source( source_obj, &self->source ) )
        return -1;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 14, 0)
    // To Anton Khirnov: This version number was difficult to find, since it
    // wasn't listed in APIchanges.
    avcodec_get_context_defaults( &self->context );
#else
    avcodec_get_context_defaults3( &self->context, codec );
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 6, 0)
    if( (error = avcodec_open( &self->context, codec )) != 0 ) {
#else
    if( (error = avcodec_open2( &self->context, codec, NULL )) != 0 ) {
#endif
        PyErr_Format( PyExc_Exception, "Could not open the codec (%s).", g_strerror( -error ) );
        py_codec_packet_take_source( NULL, &self->source );
        return -1;
    }

    g_static_mutex_init( &self->mutex );

    self->next_frame = 0;
    self->context.get_buffer = counted_get_buffer;
    self->context.release_buffer = counted_release_buffer;

    return 0;
}

static void
AVVideoDecoder_dealloc( py_obj_AVVideoDecoder *self ) {
    avcodec_close( &self->context );

    py_codec_packet_take_source( NULL, &self->source );
    g_static_mutex_free( &self->mutex );

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static coded_image *
AVVideoDecoder_get_frame( py_obj_AVVideoDecoder *self, int frame ) {
    g_static_mutex_lock( &self->mutex );

    if( self->source.source.funcs->seek && frame != self->next_frame ) {
        if( !self->source.source.funcs->seek( self->source.source.obj, frame ) ) {
            g_static_mutex_unlock( &self->mutex );
            return NULL;
        }
    }

    self->next_frame = frame;

    AVFrame av_frame;
    avcodec_get_frame_defaults( &av_frame );

    // TODO: This won't work for any format in which the packets
    // are not in presentation order

    // TODO: This also does not consider multiple frames in a single packet.
    // See http://www.inb.uni-luebeck.de/~boehme/using_libavcodec.html for example.

    // TODO: Handle when we get to the end of the stream (keep passing NULL)

    codec_packet *packet = NULL;

    for( ;; ) {
        if( packet && packet->free_func )
            packet->free_func( packet );

        packet = self->source.source.funcs->getNextPacket( self->source.source.obj );

        if( !packet ) {
            g_static_mutex_unlock( &self->mutex );
            return NULL;
        }

        int got_picture;

        //printf( "Decoding video\n" );
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 23, 0)
        avcodec_decode_video( &self->context, &av_frame, &got_picture, packet->data, packet->length );
#else
        AVPacket av_packet = {
            .pts = AV_NOPTS_VALUE,
            .dts = AV_NOPTS_VALUE,
            .data = packet->data,
            .size = packet->length };

        avcodec_decode_video2( &self->context, &av_frame, &got_picture, &av_packet );
#endif

        if( !got_picture )
            continue;

        self->next_frame = packet->pts + 1;

        if( packet->pts < frame ) {
            printf( "Too early (%" PRId64 " vs %d) (also %" PRId64 ")\n", packet->pts, frame, av_frame.pts );
            continue;
        }

        if( packet && packet->free_func )
            packet->free_func( packet );

        my_coded_image *image = (my_coded_image*) av_frame.opaque;
        g_atomic_int_inc( &image->ref_count );

        g_static_mutex_unlock( &self->mutex );

        return &image->image;
    }
}

static coded_image_source_funcs source_funcs = {
    .getFrame = (coded_image_getFrameFunc) AVVideoDecoder_get_frame,
};

static PyObject *pySourceFuncs;

static PyObject *
AVVideoDecoder_getFuncs( py_obj_AVVideoDecoder *self, void *closure ) {
    Py_INCREF(pySourceFuncs);
    return pySourceFuncs;
}

static PyGetSetDef AVVideoDecoder_getsetters[] = {
    { CODED_IMAGE_SOURCE_FUNCS, (getter) AVVideoDecoder_getFuncs, NULL, "Coded image source C API." },
    { NULL }
};

static PyMethodDef AVVideoDecoder_methods[] = {
    { NULL }
};

static PyTypeObject py_type_AVVideoDecoder = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.libav.AVVideoDecoder",
    .tp_basicsize = sizeof(py_obj_AVVideoDecoder),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_CodedImageSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AVVideoDecoder_dealloc,
    .tp_init = (initproc) AVVideoDecoder_init,
    .tp_getset = AVVideoDecoder_getsetters,
    .tp_methods = AVVideoDecoder_methods
};

void init_AVVideoDecoder( PyObject *module ) {
    if( PyType_Ready( &py_type_AVVideoDecoder ) < 0 )
        return;

    Py_INCREF( &py_type_AVVideoDecoder );
    PyModule_AddObject( module, "AVVideoDecoder", (PyObject *) &py_type_AVVideoDecoder );

    pySourceFuncs = PyCapsule_New( &source_funcs,
        CODED_IMAGE_SOURCE_FUNCS, NULL );
}



