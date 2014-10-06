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
#include <libavutil/avstring.h>

// Support old Libav
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(52, 107, 0)
#define AVIO_FLAG_WRITE URL_WRONLY
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(52, 105, 0)
#define avio_close  url_fclose
#define avio_open   url_fopen
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(52, 102, 0)
#define AVIOContext ByteIOContext
#endif
#endif
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 30, 0)
#define AV_PKT_FLAG_KEY         PKT_FLAG_KEY
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 64, 0)
#define AVMEDIA_TYPE_VIDEO      CODEC_TYPE_VIDEO
#endif

typedef struct __stream_t {
    AVStream *stream;
    CodecPacketSourceHolder source;

    rational rate;
    codec_packet *next_packet;
    int64_t next_dts;

    struct __stream_t *next;
} stream_t;

typedef struct {
    PyObject_HEAD

    AVOutputFormat *format;
    AVFormatContext *context;
    stream_t *stream_list;
    volatile bool quit;
} py_obj_AVMuxer;

static int
AVMuxer_init( py_obj_AVMuxer *self, PyObject *args, PyObject *kw ) {
    // Zero all pointers (so we know later what needs deleting)
    const char *format_name, *filename;

    static char *kwlist[] = { "filename", "format", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "ss", kwlist, &filename, &format_name ) )
        return -1;

    av_register_all();
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 45, 0)
    self->format = guess_format( format_name, NULL, NULL );        // Static ptr
#else
    self->format = av_guess_format( format_name, NULL, NULL );        // Static ptr
#endif

    if( self->format == NULL ) {
        PyErr_Format( PyExc_Exception, "Failed to find an output format named \"%s\".", format_name );
        return -1;
    }

    self->context = avformat_alloc_context();
    self->context->oformat = self->format;
    av_strlcpy( self->context->filename, filename, sizeof(self->context->filename) );

#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 2, 0)
    int error;

    // In v53, this is done in avformat_write_header
    AVFormatParameters formatParams = { { 0 } };
    if( (error = av_set_parameters( self->context, &formatParams )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Failed to set the format parameters (%s).", g_strerror( -error ) );
        return -1;
    }
#endif

    return 0;
}

static void
AVMuxer_dealloc( py_obj_AVMuxer *self ) {
    while( self->stream_list ) {
        stream_t *current = self->stream_list;
        self->stream_list = current->next;

        py_codec_packet_take_source( NULL, &current->source );

        if( current->stream->codec->extradata )
            g_slice_free1( current->stream->codec->extradata_size, current->stream->codec->extradata );

        av_free( current->stream->codec );
        av_free( current->stream );
        g_slice_free( stream_t, current );
    }

    if( self->context ) {
        av_free( self->context );
        self->context = NULL;
    }

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static void
stream_append( stream_t **list, stream_t *next ) {
    stream_t *current = *list;

    if( current == NULL ) {
        *list = next;
        return;
    }

    while( current->next )
        current = current->next;

    current->next = next;
}

static PyObject *
AVMuxer_add_video_stream( py_obj_AVMuxer *self, PyObject *args, PyObject *kw ) {
    PyObject *source_obj, *frame_rate_obj, *sample_aspect_ratio_obj, *frame_size_obj;
    int codec_id;
    int bit_rate;

    static char *kwlist[] = { "source", "codec", "frame_rate", "frame_size", "sample_aspect_ratio",
        "bit_rate", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "OiOOOi", kwlist, &source_obj, &codec_id,
            &frame_rate_obj, &frame_size_obj, &sample_aspect_ratio_obj, &bit_rate ) )
        return NULL;

    // Parse and validate the arguments
    rational sample_aspect_ratio, frame_rate;
    v2i frame_size;

    avcodec_register_all();

    if( !py_parse_rational( frame_rate_obj, &frame_rate ) )
        return NULL;

    if( !py_parse_rational( sample_aspect_ratio_obj, &sample_aspect_ratio ) )
        return NULL;

    if( !py_parse_v2i( frame_size_obj, &frame_size ) )
        return NULL;

    AVCodec *codec = avcodec_find_encoder( codec_id );

    if( !codec ) {
        PyErr_Format( PyExc_Exception, "Failed to find an encoder with the ID \"%d\".", codec_id );
        return NULL;
    }

    stream_t *stream = g_slice_new0( stream_t ); 
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 10, 0)
    stream->stream = av_new_stream( self->context, self->context->nb_streams );
#else
    stream->stream = avformat_new_stream( self->context, codec );
#endif

    if( !stream->stream )
        return PyErr_NoMemory();

    if( !py_codec_packet_take_source( source_obj, &stream->source ) ) {
        av_free( stream->stream );
        g_slice_free( stream_t, stream );
        return NULL;
    }

    // A lot of the stream's details come from the codec context, so
    // even if it's not the context we're encoding with, it needs to be set.
    // (Of course, if the format tries to read live encoding data out of the
    // context, we're screwed.)
    stream->stream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->stream->codec->codec_id = codec_id;
    stream->stream->codec->time_base = (AVRational) { .num = frame_rate.d, .den = frame_rate.n };
    stream->stream->codec->width = frame_size.x;
    stream->stream->codec->height = frame_size.y;
    stream->stream->codec->bit_rate = bit_rate;
    stream->rate = frame_rate;

    stream_append( &self->stream_list, stream );

    Py_RETURN_NONE;
}

static PyObject *
AVMuxer_add_audio_stream( py_obj_AVMuxer *self, PyObject *args, PyObject *kw ) {
    PyObject *source_obj;
    int codec_id;
    int sample_rate, channels, bit_rate;

    static char *kwlist[] = { "source", "codec", "sample_rate", "channels", "bit_rate", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "Oiiii", kwlist, &source_obj, &codec_id,
            &sample_rate, &channels, &bit_rate ) )
        return NULL;

    // Parse and validate the arguments
    avcodec_register_all();

    AVCodec *codec = avcodec_find_encoder( codec_id );

    if( !codec ) {
        PyErr_Format( PyExc_Exception, "Failed to find an encoder with the ID \"%d\".", codec_id );
        return NULL;
    }

    stream_t *stream = g_slice_new0( stream_t );
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 10, 0)
    stream->stream = av_new_stream( self->context, self->context->nb_streams );
#else
    stream->stream = avformat_new_stream( self->context, codec );
#endif

    if( !stream->stream )
        return PyErr_NoMemory();

    if( !py_codec_packet_take_source( source_obj, &stream->source ) ) {
        av_free( stream->stream );
        g_slice_free( stream_t, stream );
        return NULL;
    }

    // A lot of the stream's details come from the codec context, so
    // even if it's not the context we're encoding with, it needs to be set.
    // (Of course, if the format tries to read live encoding data out of the
    // context, we're screwed.)
    stream->stream->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    stream->stream->codec->codec_id = codec_id;
    stream->stream->codec->time_base = (AVRational) { .num = 1, .den = sample_rate };
    stream->stream->codec->sample_rate = sample_rate;
    stream->stream->codec->channels = channels;
    stream->stream->codec->bit_rate = bit_rate;
    stream->rate = (rational) { .n = sample_rate, .d = 1 };

    stream_append( &self->stream_list, stream );

    Py_RETURN_NONE;
}

static int
codec_getHeader( CodecPacketSourceHolder *holder, void *buffer ) {
    if( !holder->source.funcs->getHeader )
        return 0;

    return holder->source.funcs->getHeader( holder->source.obj, buffer );
}

static PyObject *
AVMuxer_run( py_obj_AVMuxer *self, PyObject *args, PyObject *kw ) {
    AVIOContext *stream;
    int error;

    self->quit = false;

    if( avio_open( &stream, self->context->filename, AVIO_FLAG_WRITE ) < 0 ) {
        PyErr_SetString( PyExc_Exception, "Failed to open the file." );
        return NULL;
    }

    self->context->pb = stream;

    // Seed the packet list
    PyThreadState *_save = PyEval_SaveThread();
    stream_t *current = self->stream_list;

    while( current ) {
        // Fetch the global headers (extradata)
        int header_size = codec_getHeader( &current->source, NULL );

        if( header_size ) {
            current->stream->codec->extradata = g_slice_alloc( header_size );
            current->stream->codec->extradata_size = header_size;

            codec_getHeader( &current->source, current->stream->codec->extradata );
        }

        // Get the first packet for each codec
        current->next_packet = current->source.source.funcs->getNextPacket( current->source.source.obj );

        if( current->next_packet )
            current->next_dts = get_frame_time( &current->rate, current->next_packet->dts );

        current = current->next;
    }

    // Write format header
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 2, 0)
    if( (error = av_write_header( self->context )) < 0 ) {
#else
    if( (error = avformat_write_header( self->context, NULL )) < 0 ) {
#endif
        PyEval_RestoreThread( _save );
        PyErr_Format( PyExc_Exception, "Failed to write the header (%s).", g_strerror( -error ) );
        return NULL;
    }

    // Write the packets
    for( ;; ) {
        if( self->quit ) {
            PyEval_RestoreThread( _save );
            avio_close( stream );
            Py_RETURN_NONE;
        }

        if( PyErr_CheckSignals() ) {
            PyEval_RestoreThread( _save );
            avio_close( stream );
            return NULL;
        }

        AVPacket packet;
        av_init_packet( &packet );

        stream_t *next_stream = NULL;
        current = self->stream_list;

        while( current ) {
            if( current->next_packet ) {
                if( !next_stream || current->next_dts < next_stream->next_dts )
                    next_stream = current;
            }

            current = current->next;
        }

        if( !next_stream )
            break;

        packet.stream_index = next_stream->stream->index;
        packet.data = next_stream->next_packet->data;
        packet.size = next_stream->next_packet->length;
        packet.pts = next_stream->next_packet->pts;
        packet.dts = next_stream->next_packet->dts;

        if( next_stream->next_packet->keyframe )
            packet.flags |= AV_PKT_FLAG_KEY;

        if( (error = av_interleaved_write_frame( self->context, &packet )) < 0 ) {
            PyEval_RestoreThread( _save );
            PyErr_Format( PyExc_Exception, "Failed to write frame (%s).", g_strerror( -error ) );
            avio_close( stream );
            return NULL;
        }

        // Set up next packet
        if( next_stream->next_packet->free_func )
            next_stream->next_packet->free_func( next_stream->next_packet );

        next_stream->next_packet = next_stream->source.source.funcs->getNextPacket( next_stream->source.source.obj );

        if( next_stream->next_packet )
            next_stream->next_dts = get_frame_time( &next_stream->rate, next_stream->next_packet->dts );
    }

    // Close format
    if( (error = av_write_trailer( self->context )) < 0 ) {
        PyEval_RestoreThread( _save );
        PyErr_Format( PyExc_Exception, "Failed to write format trailer (%s).", g_strerror( -error ) );
        avio_close( stream );
        return NULL;
    }

    // Close file
    avio_close( stream );

    PyEval_RestoreThread( _save );
    Py_RETURN_NONE;
}

static PyObject *
AVMuxer_cancel( py_obj_AVMuxer *self, PyObject *args, PyObject *kw ) {
    self->quit = true;
    Py_RETURN_NONE;
}

static PyGetSetDef AVMuxer_getsetters[] = {
    { NULL }
};

static PyMethodDef AVMuxer_methods[] = {
    { "add_video_stream", (PyCFunction) AVMuxer_add_video_stream, METH_VARARGS | METH_KEYWORDS,
        "Add a video stream to the output.\n"
        "\n"
        "muxer.add_video_stream(source, codec, frame_rate, frame_size, sample_aspect_ratio)\n"
        "\n"
        "source - A codec packet source.\n"
        "codec - The name of the Libav encoder for the codec. The actual codec used\n"
        "    doesn't have to be an Libav codec, but Libav uses its codec definitions to\n"
        "    identify the codec in the file. (In the future, this parameter will accept Libav codec ID's.)\n"
        "frame_rate - The video frame rate as a rational.\n"
        "frame_size - A v2i value with the size of the frame.\n"
        "sample_aspect_ratio - The sample aspect ratio as a rational." },
    { "add_audio_stream", (PyCFunction) AVMuxer_add_audio_stream, METH_VARARGS | METH_KEYWORDS },
    { "run", (PyCFunction) AVMuxer_run, METH_NOARGS,
        "Run the muxer and write the complete file." },
    { "cancel", (PyCFunction) AVMuxer_cancel, METH_NOARGS,
        "Cancel a current run() call." },
    { NULL }
};

static PyTypeObject py_type_AVMuxer = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.libav.AVMuxer",
    .tp_basicsize = sizeof(py_obj_AVMuxer),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AVMuxer_dealloc,
    .tp_init = (initproc) AVMuxer_init,
    .tp_getset = AVMuxer_getsetters,
    .tp_methods = AVMuxer_methods
};

void init_AVMuxer( PyObject *module ) {
    if( PyType_Ready( &py_type_AVMuxer ) < 0 )
        return;

    Py_INCREF( &py_type_AVMuxer );
    PyModule_AddObject( module, "AVMuxer", (PyObject *) &py_type_AVMuxer );
}



