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
#include <libavutil/mem.h>
#include <libavcodec/avcodec.h>

typedef struct {
    PyObject_HEAD

    CodedImageSourceHolder source;
    AVCodecContext* context;
    bool interlaced, top_field_first;
    int start_frame, current_frame, end_frame;
} py_obj_AVVideoEncoder;

static int
AVVideoEncoder_init( py_obj_AVVideoEncoder *self, PyObject *args, PyObject *kw ) {
    int error;

    PyObject *source_obj, *frame_rate_obj, *sample_aspect_ratio_obj, *frame_size_obj,
        *interlaced_obj, *global_header_obj, *top_field_first_obj;
    const char *codec_name;

    static char *kwlist[] = { "source", "codec", "start_frame", "end_frame", "frame_rate", "frame_size",
        "sample_aspect_ratio", "interlaced", "global_header", "top_field_first", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "OsiiOOO|OOO", kwlist, &source_obj, &codec_name,
            &self->current_frame, &self->end_frame, &frame_rate_obj, &frame_size_obj, &sample_aspect_ratio_obj, &interlaced_obj,
            &global_header_obj, &top_field_first_obj ) )
        return -1;

    self->start_frame = self->current_frame;

    // Parse and validate the arguments
    avcodec_register_all();
    AVCodec *codec = avcodec_find_encoder_by_name( codec_name );

    if( !codec ) {
        PyErr_Format( PyExc_Exception, "Could not find the codec \"%s\".", codec_name );
        return -1;
    }

    rational sample_aspect_ratio, frame_rate;
    v2i frame_size;

    if( !py_coded_image_take_source( source_obj, &self->source ) )
        return -1;

    if( !py_parse_rational( frame_rate_obj, &frame_rate ) )
        return -1;

    if( !py_parse_rational( sample_aspect_ratio_obj, &sample_aspect_ratio ) )
        return -1;

    if( !py_parse_v2i( frame_size_obj, &frame_size ) )
        return -1;

    // Prepare the context
    // BJC: I apologize, as I don't know how this is meant to be written in earlier
    // versions of libav; if you encounter compile errors here and can fix it,
    // send me a patch
    self->context = avcodec_alloc_context3( codec );

    if( interlaced_obj && (PyObject_IsTrue( interlaced_obj ) == 1) ) {
        self->context->flags |= CODEC_FLAG_INTERLACED_DCT;
        self->interlaced = true;
    }

    if( top_field_first_obj && (PyObject_IsTrue( top_field_first_obj ) == 1) )
        self->top_field_first = true;

    if( global_header_obj && (PyObject_IsTrue( global_header_obj ) == 1) )
        self->context->flags |= CODEC_FLAG_GLOBAL_HEADER;

    //if (codec && codec->supported_framerates && !force_fps)
    //    fps = codec->supported_framerates[av_find_nearest_q_idx(fps, codec->supported_framerates)];
    self->context->time_base.den = frame_rate.n;
    self->context->time_base.num = frame_rate.d;

    self->context->width = frame_size.x;
    self->context->height = frame_size.y;
    self->context->sample_aspect_ratio.num = sample_aspect_ratio.n;
    self->context->sample_aspect_ratio.den = sample_aspect_ratio.d;
    self->context->pix_fmt = PIX_FMT_YUV411P;       // TODO

    // Open the codec
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 6, 0)
    if( (error = avcodec_open( self->context, codec )) != 0 ) {
#else
    // TODO: Pass codec options in as dictionary
    if( (error = avcodec_open2( self->context, codec, NULL )) != 0 ) {
#endif
        PyErr_Format( PyExc_Exception, "Could not open the codec (%s).", g_strerror( -error ) );
        py_coded_image_take_source( NULL, &self->source );

        avcodec_close( self->context );
        av_free( self->context );
        self->context = NULL;

        return -1;
    }

    return 0;
}

static void
AVVideoEncoder_dealloc( py_obj_AVVideoEncoder *self ) {
    if( self->context ) {
        avcodec_close( self->context );
        av_free( self->context );
        self->context = NULL;
    }

    py_coded_image_take_source( NULL, &self->source );
    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static void
my_packet_free( codec_packet *packet ) {
    g_assert(packet);
    g_slice_free1( packet->length, packet->data );
    g_slice_free( codec_packet, packet );
}

static int
AVVideoEncoder_get_header( py_obj_AVVideoEncoder *self, void *buffer ) {
    if( !self->context->extradata )
        return 0;

    if( !buffer )
        return self->context->extradata_size;

    memcpy( buffer, self->context->extradata, self->context->extradata_size );
    return 1;
}

static codec_packet *
AVVideoEncoder_get_next_packet( py_obj_AVVideoEncoder *self ) {
    AVPacket avpacket = { .data = NULL };
    codec_packet *packet;
    int got_packet;
    int result;

    while( self->current_frame <= self->end_frame ) {
        coded_image *image = self->source.source.funcs->getFrame( self->source.source.obj, self->current_frame, 0 );

        if( !image ) {
            // TODO: Report an error instead of ending the stream early
            self->current_frame = self->end_frame;
            continue;
        }

        AVFrame output_frame = {
            .data = { image->data[0], image->data[1], image->data[2], image->data[3] },
            .linesize = { image->stride[0], image->stride[1], image->stride[2], image->stride[3] },
            .pts = AV_NOPTS_VALUE,      // Will this need changing for other codecs?
            .interlaced_frame = self->interlaced ? 1 : 0,
            .top_field_first = self->top_field_first ? 1 : 0,
        };

        avpacket = (AVPacket) { .data = NULL };

        result = avcodec_encode_video2( self->context,
            &avpacket, &output_frame, &got_packet );

        if( image->free_func )
            image->free_func( image );

        if( result < 0 ) {
            // TODO: Report error properly
            av_free_packet( &avpacket );
            return NULL;
        }

        self->current_frame++;

        if( !got_packet )
            continue;

        packet = g_slice_new0( codec_packet );
        packet->length = avpacket.size;
        packet->data = g_slice_copy( avpacket.size, avpacket.data );
        packet->pts = self->context->coded_frame->pts;
        packet->dts = PACKET_TS_NONE;
        packet->keyframe = self->context->coded_frame->key_frame ? true : false;
        packet->free_func = (GFreeFunc) my_packet_free;

        // I considered here that, instead of copying, we *could* use the packet buffer that
        // was allocated for us. The tricky bit is that we don't have a guarantee how long
        // the returned buffer will last. If the destructor is set in AVPacket, we can keep
        // the data and throw it away later. If the destructor is not set, the data will
        // disappear on the next decode call, and so we would have to copy it.
        av_free_packet( &avpacket );

        return packet;
    }

    avpacket = (AVPacket) { .data = NULL };

    result = avcodec_encode_video2( self->context,
        &avpacket, NULL, &got_packet );

    if( result < 0 ) {
        // TODO: Report error properly
        av_free_packet( &avpacket );
        return NULL;
    }

    if( !got_packet ) {
        // Not an error
        return NULL;
    }

    packet = g_slice_new0( codec_packet );
    packet->length = avpacket.size;
    packet->data = g_slice_copy( avpacket.size, avpacket.data );
    packet->pts = self->context->coded_frame->pts;
    packet->dts = PACKET_TS_NONE;
    packet->keyframe = self->context->coded_frame->key_frame ? true : false;
    packet->free_func = (GFreeFunc) my_packet_free;

    av_free_packet( &avpacket );

    return packet;
}

static codec_packet_source_funcs source_funcs = {
    .getHeader = (codec_getHeaderFunc) AVVideoEncoder_get_header,
    .getNextPacket = (codec_getNextPacketFunc) AVVideoEncoder_get_next_packet,
};

static PyObject *pySourceFuncs;

static PyObject *
AVVideoEncoder_getFuncs( py_obj_AVVideoEncoder *self, void *closure ) {
    Py_INCREF(pySourceFuncs);
    return pySourceFuncs;
}

static PyObject *
AVVideoEncoder_get_progress( py_obj_AVVideoEncoder *self, void *closure ) {
    return PyLong_FromLong( self->current_frame - self->start_frame );
}

static PyObject *
AVVideoEncoder_get_progress_count( py_obj_AVVideoEncoder *self, void *closure ) {
    return PyLong_FromLong( self->end_frame - self->start_frame );
}

static PyGetSetDef AVVideoEncoder_getsetters[] = {
    { CODEC_PACKET_SOURCE_FUNCS, (getter) AVVideoEncoder_getFuncs, NULL, "Codec packet source C API." },
    { "progress", (getter) AVVideoEncoder_get_progress, NULL, "Encoder progress, from zero to progress_count." },
    { "progress_count", (getter) AVVideoEncoder_get_progress_count, NULL, "Number of items to complete. Compare to progress." },
    { NULL }
};

static PyMethodDef AVVideoEncoder_methods[] = {
    { NULL }
};

static PyTypeObject py_type_AVVideoEncoder = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.libav.AVVideoEncoder",
    .tp_basicsize = sizeof(py_obj_AVVideoEncoder),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_CodecPacketSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AVVideoEncoder_dealloc,
    .tp_init = (initproc) AVVideoEncoder_init,
    .tp_getset = AVVideoEncoder_getsetters,
    .tp_methods = AVVideoEncoder_methods
};

void init_AVVideoEncoder( PyObject *module ) {
    if( PyType_Ready( &py_type_AVVideoEncoder ) < 0 )
        return;

    Py_INCREF( &py_type_AVVideoEncoder );
    PyModule_AddObject( module, "AVVideoEncoder", (PyObject *) &py_type_AVVideoEncoder );

    pySourceFuncs = PyCapsule_New( &source_funcs,
        CODEC_PACKET_SOURCE_FUNCS, NULL );
}



