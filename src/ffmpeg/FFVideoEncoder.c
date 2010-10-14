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
#include <libavcodec/avcodec.h>

typedef struct {
    PyObject_HEAD

    CodedImageSourceHolder source;
    AVCodecContext context;
    bool context_initialized;
    bool interlaced, top_field_first;
    int start_frame, current_frame, end_frame;
} py_obj_FFVideoEncoder;

static int
FFVideoEncoder_init( py_obj_FFVideoEncoder *self, PyObject *args, PyObject *kw ) {
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
    avcodec_get_context_defaults( &self->context );

    self->context.codec_id = codec->id;

    if( interlaced_obj && (PyObject_IsTrue( interlaced_obj ) == 1) ) {
        self->context.flags |= CODEC_FLAG_INTERLACED_DCT;
        self->interlaced = true;
    }

    if( top_field_first_obj && (PyObject_IsTrue( top_field_first_obj ) == 1) )
        self->top_field_first = true;

    if( global_header_obj && (PyObject_IsTrue( global_header_obj ) == 1) )
        self->context.flags |= CODEC_FLAG_GLOBAL_HEADER;

    //if (codec && codec->supported_framerates && !force_fps)
    //    fps = codec->supported_framerates[av_find_nearest_q_idx(fps, codec->supported_framerates)];
    self->context.time_base.den = frame_rate.n;
    self->context.time_base.num = frame_rate.d;

    self->context.width = frame_size.x;
    self->context.height = frame_size.y;
    self->context.sample_aspect_ratio.num = sample_aspect_ratio.n;
    self->context.sample_aspect_ratio.den = sample_aspect_ratio.d;
    self->context.pix_fmt = PIX_FMT_YUV411P;       // TODO

    // Open the codec
    if( (error = avcodec_open( &self->context, codec )) != 0 ) {
        PyErr_Format( PyExc_Exception, "Could not open the codec (%s).", g_strerror( -error ) );
        py_coded_image_take_source( NULL, &self->source );
        return -1;
    }

    self->context_initialized = true;

    return 0;
}

static void
FFVideoEncoder_dealloc( py_obj_FFVideoEncoder *self ) {
    if( self->context_initialized )
        avcodec_close( &self->context );

    self->ob_type->tp_free( (PyObject*) self );
}

static void
my_packet_free( codec_packet *packet ) {
    g_assert(packet);
    g_slice_free1( packet->length, packet->data );
    g_slice_free( codec_packet, packet );
}

static int
FFVideoEncoder_get_header( py_obj_FFVideoEncoder *self, void *buffer ) {
    if( !self->context.extradata )
        return 0;

    if( !buffer )
        return self->context.extradata_size;

    memcpy( buffer, self->context.extradata, self->context.extradata_size );
    return 1;
}

static codec_packet *
FFVideoEncoder_get_next_packet( py_obj_FFVideoEncoder *self ) {
    // Would be happy to have a better size estimate for a bit bucket
    int bit_bucket_size = (self->context.width * self->context.height) * 6 + 200;
    void *bit_bucket = g_malloc( bit_bucket_size );
    codec_packet *packet;
    int result;

    while( self->current_frame < self->end_frame ) {
        coded_image *image = self->source.source.funcs->getFrame( self->source.source.obj, self->current_frame );

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
        int result;

        result = avcodec_encode_video( &self->context,
            bit_bucket, bit_bucket_size, &output_frame );

        if( image->free_func )
            image->free_func( image );

        if( result < 0 ) {
            // TODO: Report error properly
            g_free( bit_bucket );
            return NULL;
        }

        self->current_frame++;

        if( result == 0 )
            continue;

        packet = g_slice_new0( codec_packet );
        packet->length = result;
        packet->data = g_slice_copy( result, bit_bucket );
        packet->pts = self->context.coded_frame->pts;
        packet->dts = PACKET_TS_NONE;
        packet->keyframe = self->context.coded_frame->key_frame ? true : false;
        packet->free_func = (GFreeFunc) my_packet_free;

        g_free( bit_bucket );

        return packet;
    }

    result = avcodec_encode_video( &self->context,
        bit_bucket, bit_bucket_size, NULL );

    if( result < 0 ) {
        // TODO: Report error properly
        g_free( bit_bucket );
        return NULL;
    }

    if( result == 0 ) {
        // Not an error
        g_free( bit_bucket );
        return NULL;
    }

    packet = g_slice_new0( codec_packet );
    packet->length = result;
    packet->data = g_slice_copy( result, bit_bucket );
    packet->pts = self->context.coded_frame->pts;
    packet->dts = PACKET_TS_NONE;
    packet->keyframe = self->context.coded_frame->key_frame ? true : false;
    packet->free_func = (GFreeFunc) my_packet_free;

    g_free( bit_bucket );

    return packet;
}

static codec_packet_source_funcs source_funcs = {
    .getHeader = (codec_getHeaderFunc) FFVideoEncoder_get_header,
    .getNextPacket = (codec_getNextPacketFunc) FFVideoEncoder_get_next_packet,
};

static PyObject *pySourceFuncs;

static PyObject *
FFVideoEncoder_getFuncs( py_obj_FFVideoEncoder *self, void *closure ) {
    Py_INCREF(pySourceFuncs);
    return pySourceFuncs;
}

static PyObject *
FFVideoEncoder_get_progress( py_obj_FFVideoEncoder *self, void *closure ) {
    return PyInt_FromLong( self->current_frame - self->start_frame );
}

static PyObject *
FFVideoEncoder_get_progress_count( py_obj_FFVideoEncoder *self, void *closure ) {
    return PyInt_FromLong( self->end_frame - self->start_frame );
}

static PyGetSetDef FFVideoEncoder_getsetters[] = {
    { CODEC_PACKET_SOURCE_FUNCS, (getter) FFVideoEncoder_getFuncs, NULL, "Codec packet source C API." },
    { "progress", (getter) FFVideoEncoder_get_progress, NULL, "Encoder progress, from zero to progress_count." },
    { "progress_count", (getter) FFVideoEncoder_get_progress_count, NULL, "Number of items to complete. Compare to progress." },
    { NULL }
};

static PyMethodDef FFVideoEncoder_methods[] = {
    { NULL }
};

static PyTypeObject py_type_FFVideoEncoder = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.ffmpeg.FFVideoEncoder",    // tp_name
    sizeof(py_obj_FFVideoEncoder),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_CodecPacketSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) FFVideoEncoder_dealloc,
    .tp_init = (initproc) FFVideoEncoder_init,
    .tp_getset = FFVideoEncoder_getsetters,
    .tp_methods = FFVideoEncoder_methods
};

void init_FFVideoEncoder( PyObject *module ) {
    if( PyType_Ready( &py_type_FFVideoEncoder ) < 0 )
        return;

    Py_INCREF( &py_type_FFVideoEncoder );
    PyModule_AddObject( module, "FFVideoEncoder", (PyObject *) &py_type_FFVideoEncoder );

    pySourceFuncs = PyCObject_FromVoidPtr( &source_funcs, NULL );
}



