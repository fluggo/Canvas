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

// Support old Libav
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 64, 0)
#define AVMEDIA_TYPE_VIDEO      CODEC_TYPE_VIDEO
#define AVMEDIA_TYPE_AUDIO      CODEC_TYPE_AUDIO
#define AVMEDIA_TYPE_DATA       CODEC_TYPE_DATA
#define AVMEDIA_TYPE_SUBTITLE   CODEC_TYPE_SUBTITLE
#define AVMEDIA_TYPE_ATTACHMENT CODEC_TYPE_ATTACHMENT
#define AVMEDIA_TYPE_NB         CODEC_TYPE_NB
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 30, 0)
#define AV_PKT_FLAG_KEY         PKT_FLAG_KEY
#endif

typedef struct {
    PyObject_HEAD

    AVFormatContext *context;
    AVCodecContext *codecContext;
    int stream;
    bool raw_timestamps;
    rational frame_duration;
    GMutex mutex;
} py_obj_AVDemuxer;

static int
AVDemuxer_init( py_obj_AVDemuxer *self, PyObject *args, PyObject *kw ) {
    int error;
    char *filename;
    PyObject *raw_timestamp_obj = NULL;

    // Zero all pointers (so we know later what needs deleting)
    self->context = NULL;
    self->codecContext = NULL;
    self->raw_timestamps = false;

    static char *kwlist[] = { "filename", "stream", "raw_timestamps", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "si|O", kwlist, &filename, &self->stream, &raw_timestamp_obj ) )
        return -1;

    if( raw_timestamp_obj )
        self->raw_timestamps = (PyObject_IsTrue( raw_timestamp_obj ) == 1);

    av_register_all();

#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 2, 0)
    if( (error = av_open_input_file( &self->context, filename, NULL, 0, NULL )) != 0 ) {
#else
    if( (error = avformat_open_input( &self->context, filename, NULL, NULL )) != 0 ) {
#endif
        PyErr_Format( PyExc_Exception, "Could not open the file (%s).", g_strerror( -error ) );
        return -1;
    }

#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 3, 0)
    if( (error = av_find_stream_info( self->context )) < 0 ) {
#else
    if( (error = avformat_find_stream_info( self->context, NULL )) < 0 ) {
#endif
        PyErr_Format( PyExc_Exception, "Could not find the stream info (%s).", g_strerror( -error ) );
        return -1;
    }

    if( self->stream >= self->context->nb_streams ) {
        PyErr_Format( PyExc_Exception, "The given stream number %i was not found in the file.", self->stream );
        return -1;
    }

    self->codecContext = self->context->streams[self->stream]->codec;

    g_mutex_init( &self->mutex );

    // Calculate the frame (sample) duration
    if( self->codecContext->codec_type == AVMEDIA_TYPE_VIDEO ) {
        // This formula should be right for most cases, except, of course, when r_frame_rate is wrong
        AVRational *timeBase = &self->context->streams[self->stream]->time_base;
        AVRational *frameRate = &self->context->streams[self->stream]->r_frame_rate;

        if( !frameRate->num ) {
            // Frame rate wasn't specified, guess that it's the inverse of the time base
            self->frame_duration.n = 1;
            self->frame_duration.d = 1;
        }
        else {
            self->frame_duration.n = timeBase->den * frameRate->den;
            self->frame_duration.d = timeBase->num * frameRate->num;
        }
    }
    else if( self->codecContext->codec_type == AVMEDIA_TYPE_AUDIO ) {
        AVRational *timeBase = &self->context->streams[self->stream]->time_base;
        int sampleRate = self->codecContext->sample_rate;

        self->frame_duration.n = timeBase->den;
        self->frame_duration.d = timeBase->num * sampleRate;
    }

    return 0;
}

static void
AVDemuxer_dealloc( py_obj_AVDemuxer *self ) {
    if( self->context != NULL ) {
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 17, 0)
        av_close_input_file( self->context );
        self->context = NULL;
#else
        avformat_close_input( &self->context );
#endif
    }

    g_mutex_clear( &self->mutex );

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static bool
AVDemuxer_seek( py_obj_AVDemuxer *self, int64_t frame ) {
    int64_t timestamp;

    if( self->raw_timestamps )
        timestamp = frame;
    else
        timestamp = (frame * self->frame_duration.n) / self->frame_duration.d;

    if( av_seek_frame( self->context, self->stream, timestamp, AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD ) < 0 ) {
        printf( "Could not seek to frame %" PRId64 ".\n", frame );
        return false;
    }

    return true;
}

typedef struct __tag_my_packet {
    codec_packet packet;
    AVPacket av_packet;
} my_packet;

static void
my_packet_free( my_packet *packet ) {
    av_free_packet( &packet->av_packet );
    g_slice_free( my_packet, packet );
}

static int
AVDemuxer_get_header( py_obj_AVDemuxer *self, void *buffer ) {
    if( !self->codecContext->extradata )
        return 0;

    if( !buffer )
        return self->codecContext->extradata_size;

    memcpy( buffer, self->codecContext->extradata, self->codecContext->extradata_size );
    return 1;
}

static codec_packet *
AVDemuxer_get_next_packet( py_obj_AVDemuxer *self ) {
    my_packet *packet = g_slice_new0( my_packet );
    av_init_packet( &packet->av_packet );

    g_mutex_lock( &self->mutex );

    for( ;; ) {
        //printf( "Reading frame\n" );
        if( av_read_frame( self->context, &packet->av_packet ) < 0 ) {
            g_mutex_unlock( &self->mutex );
            my_packet_free( packet );

            return NULL;
        }

        if( packet->av_packet.stream_index == self->stream )
            break;

        av_free_packet( &packet->av_packet );
    }

    g_mutex_unlock( &self->mutex );

    packet->packet.data = packet->av_packet.data;
    packet->packet.length = packet->av_packet.size;
    packet->packet.dts = packet->av_packet.dts;
    packet->packet.pts = packet->av_packet.pts;
    packet->packet.duration = packet->av_packet.duration;
    packet->packet.free_func = (GFreeFunc) my_packet_free;

    // TODO: There are a lot of cases where this won't work
    if( packet->packet.pts == PACKET_TS_NONE ) {
        g_warning( "Libav format did not supply pts, dts is %" PRId64 ".", packet->packet.dts );
        packet->packet.pts = packet->packet.dts;
    }

    packet->packet.keyframe = (packet->av_packet.flags & AV_PKT_FLAG_KEY) ? true : false;

    // Convert timestamps from raw to frames/samples
    if( !self->raw_timestamps ) {
        if( packet->packet.dts != PACKET_TS_NONE )
            packet->packet.dts = (packet->av_packet.dts * self->frame_duration.d + self->frame_duration.n / 2) / self->frame_duration.n;

        if( packet->packet.pts != PACKET_TS_NONE )
            packet->packet.pts = (packet->av_packet.pts * self->frame_duration.d + self->frame_duration.n / 2) / self->frame_duration.n;

        if( packet->packet.duration != 0 )
            packet->packet.duration = (packet->av_packet.duration * self->frame_duration.d + self->frame_duration.n / 2) / self->frame_duration.n;
    }

    return (codec_packet *) packet;
}

static codec_packet_source_funcs source_funcs = {
    .getHeader = (codec_getHeaderFunc) AVDemuxer_get_header,
    .getNextPacket = (codec_getNextPacketFunc) AVDemuxer_get_next_packet,
    .seek = (codec_seekFunc) AVDemuxer_seek,
};

static PyObject *pySourceFuncs;

static PyObject *
AVDemuxer_getFuncs( py_obj_AVDemuxer *self, void *closure ) {
    Py_INCREF(pySourceFuncs);
    return pySourceFuncs;
}

static PyGetSetDef AVDemuxer_getsetters[] = {
    { CODEC_PACKET_SOURCE_FUNCS, (getter) AVDemuxer_getFuncs, NULL, "Codec packet source C API." },
    { NULL }
};

static PyMethodDef AVDemuxer_methods[] = {
    { NULL }
};

static PyTypeObject py_type_AVDemuxer = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.libav.AVDemuxer",
    .tp_basicsize = sizeof(py_obj_AVDemuxer),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_CodecPacketSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AVDemuxer_dealloc,
    .tp_init = (initproc) AVDemuxer_init,
    .tp_getset = AVDemuxer_getsetters,
    .tp_methods = AVDemuxer_methods
};

void init_AVDemuxer( PyObject *module ) {
    if( PyType_Ready( &py_type_AVDemuxer ) < 0 )
        return;

    Py_INCREF( &py_type_AVDemuxer );
    PyModule_AddObject( module, "AVDemuxer", (PyObject *) &py_type_AVDemuxer );

    pySourceFuncs = PyCapsule_New( &source_funcs,
        CODEC_PACKET_SOURCE_FUNCS, NULL );
}



