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

// Support old Libav
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 64, 0)
#define AVMEDIA_TYPE_VIDEO      CODEC_TYPE_VIDEO
#define AVMEDIA_TYPE_AUDIO      CODEC_TYPE_AUDIO
#define AVMEDIA_TYPE_DATA       CODEC_TYPE_DATA
#define AVMEDIA_TYPE_SUBTITLE   CODEC_TYPE_SUBTITLE
#define AVMEDIA_TYPE_ATTACHMENT CODEC_TYPE_ATTACHMENT
#define AVMEDIA_TYPE_NB         CODEC_TYPE_NB
#endif

typedef struct {
    PyObject_HEAD

    AVFormatContext *format;
    AVOutputFormat *out;
    PyObject *streamList;
} py_obj_AVContainer;

typedef struct {
    PyObject_HEAD

    py_obj_AVContainer *container;
    AVStream *stream;
} py_obj_AVStream;

static int
AVStream_init( py_obj_AVStream *self, PyObject *args, PyObject *kwds ) {
    self->container = NULL;
    self->stream = NULL;

    return 0;
}

static void
AVStream_dealloc( py_obj_AVStream *self ) {
    Py_CLEAR( self->container );
    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static PyTypeObject py_type_AVStream = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.libav.AVStream",
    .tp_basicsize = sizeof(py_obj_AVStream),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AVStream_dealloc,
    .tp_init = (initproc) AVStream_init,
};

static int
AVContainer_init( py_obj_AVContainer *self, PyObject *args, PyObject *kwds ) {
    int error;
    char *filename;

    // Zero all pointers (so we know later what needs deleting)
    self->format = NULL;

    if( !PyArg_ParseTuple( args, "s", &filename ) )
        return -1;

    av_register_all();

#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 2, 0)
    if( (error = av_open_input_file( &self->format, filename, NULL, 0, NULL )) != 0 ) {
#else
    if( (error = avformat_open_input( &self->format, filename, NULL, NULL )) != 0 ) {
#endif
        PyErr_Format( PyExc_Exception, "Could not open the file (%s).", g_strerror( -error ) );
        return -1;
    }

#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 3, 0)
    if( (error = av_find_stream_info( self->format )) < 0 ) {
#else
    if( (error = avformat_find_stream_info( self->format, NULL )) < 0 ) {
#endif
        PyErr_Format( PyExc_Exception, "Could not find the stream info (%s).", g_strerror( -error ) );
        return -1;
    }

    self->streamList = PyList_New( self->format->nb_streams );

    for( int i = 0; i < self->format->nb_streams; i++ ) {
        py_obj_AVStream *stream = (py_obj_AVStream *) PyObject_CallObject( (PyObject *) &py_type_AVStream, NULL );
        Py_INCREF( self );
        stream->container = self;
        stream->stream = self->format->streams[i];

        PyList_SET_ITEM( self->streamList, i, (PyObject *) stream );
    }

#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(52, 45, 0)
    // Fetch the output format, too, for the MIME type
    self->out = guess_format( self->format->iformat->name, NULL, NULL );
#else
    // Fetch the output format, too, for the MIME type
    self->out = av_guess_format( self->format->iformat->name, NULL, NULL );
#endif

    return 0;
}

static void
AVContainer_dealloc( py_obj_AVContainer *self ) {
    Py_CLEAR( self->streamList );

    if( self->format != NULL ) {
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 17, 0)
        av_close_input_file( self->format );
        self->format = NULL;
#else
        avformat_close_input( &self->format );
#endif
    }

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static PyObject *
AVContainer_formatName( py_obj_AVContainer *self, void *closure ) {
    return PyUnicode_DecodeASCII( self->format->iformat->name,
        strlen(self->format->iformat->name), NULL );
}

static PyObject *
AVContainer_formatLongName( py_obj_AVContainer *self, void *closure ) {
    return PyUnicode_DecodeASCII( self->format->iformat->long_name,
        strlen(self->format->iformat->long_name), NULL );
}

static PyObject *
AVContainer_mimeType( py_obj_AVContainer *self, void *closure ) {
    if( !self->out || !self->out->mime_type )
        Py_RETURN_NONE;

    return PyUnicode_DecodeASCII( self->out->mime_type,
        strlen(self->out->mime_type), NULL );
}

static PyObject *
AVContainer_bitRate( py_obj_AVContainer *self, void *closure ) {
    return PyLong_FromLong( self->format->bit_rate );
}

static PyObject *
AVContainer_streams( py_obj_AVContainer *self, void *closure ) {
    Py_INCREF( self->streamList );
    return self->streamList;
}

static PyObject *
AVContainer_duration( py_obj_AVContainer *self, void *closure ) {
    return PyLong_FromLongLong( self->format->duration );
}

static PyGetSetDef AVContainer_getsetters[] = {
    { "format_name", (getter) AVContainer_formatName, NULL, "The short name of the container format." },
    { "format_long_name", (getter) AVContainer_formatLongName, NULL, "A more descriptive name of the container format." },
    { "bit_rate", (getter) AVContainer_bitRate, NULL, "The bit rate of the file in bit/s." },
    { "streams", (getter) AVContainer_streams, NULL, "List of stream descriptors found in the container." },
    { "mime_type", (getter) AVContainer_mimeType, NULL, "The MIME type of the format, if known." },
    { "duration", (getter) AVContainer_duration, NULL, "The file's (estimated) duration in microseconds." },
    { NULL }
};

static PyObject *
AVStream_timeBase( py_obj_AVStream *self, void *closure ) {
    rational result = { .n = self->stream->time_base.num, .d = self->stream->time_base.den };
    return py_make_rational( &result );
}

static PyObject *
AVStream_realFrameRate( py_obj_AVStream *self, void *closure ) {
    rational result = { .n = self->stream->r_frame_rate.num, .d = self->stream->r_frame_rate.den };

    if( result.n == 0 )
        Py_RETURN_NONE;

    return py_make_rational( &result );
}

static PyObject *
AVStream_sampleAspectRatio( py_obj_AVStream *self, void *closure ) {
    AVRational sar = self->stream->sample_aspect_ratio;

    if( sar.num == 0 )
        sar = self->stream->codec->sample_aspect_ratio;

    if( sar.num == 0 )
        Py_RETURN_NONE;

    rational result = { .n = sar.num, .d = sar.den };
    return py_make_rational( &result );
}

static PyObject *pixFmtLookup[PIX_FMT_NB];
static PyObject *streamTypeLookup[AVMEDIA_TYPE_NB];

static PyObject *
AVStream_pixelFormat( py_obj_AVStream *self, void *closure ) {
    if( self->stream->codec->pix_fmt < 0 )
        Py_RETURN_NONE;

    if( self->stream->codec->pix_fmt < PIX_FMT_NB ) {
        PyObject *result = pixFmtLookup[self->stream->codec->pix_fmt];
        Py_INCREF( result );
        return result;
    }

    Py_RETURN_NONE;
}

static PyObject *
AVStream_type( py_obj_AVStream *self, void *closure ) {
    if( self->stream->codec->codec_type < 0 )
        Py_RETURN_NONE;

    if( self->stream->codec->codec_type < AVMEDIA_TYPE_NB ) {
        PyObject *result = streamTypeLookup[self->stream->codec->codec_type];
        Py_INCREF( result );
        return result;
    }

    Py_RETURN_NONE;
}

static PyObject *
AVStream_index( py_obj_AVStream *self, void *closure ) {
    return PyLong_FromLong( self->stream->index );
}

static PyObject *
AVStream_id( py_obj_AVStream *self, void *closure ) {
    return PyLong_FromLong( self->stream->id );
}

static PyObject *
AVStream_bitRate( py_obj_AVStream *self, void *closure ) {
    if( self->stream->codec->bit_rate )
        return PyLong_FromLong( self->stream->codec->bit_rate );

    Py_RETURN_NONE;
}

static PyObject *
AVStream_frameSize( py_obj_AVStream *self, void *closure ) {
    if( self->stream->codec->codec_type == AVMEDIA_TYPE_VIDEO )
        return Py_BuildValue( "ii", self->stream->codec->width, self->stream->codec->height );

    Py_RETURN_NONE;
}

static PyObject *
AVStream_sampleRate( py_obj_AVStream *self, void *closure ) {
    if( self->stream->codec->codec_type == AVMEDIA_TYPE_AUDIO )
        return PyLong_FromLong( self->stream->codec->sample_rate );

    Py_RETURN_NONE;
}

static PyObject *
AVStream_channels( py_obj_AVStream *self, void *closure ) {
    if( self->stream->codec->codec_type == AVMEDIA_TYPE_AUDIO )
        return PyLong_FromLong( self->stream->codec->channels );

    Py_RETURN_NONE;
}

static PyObject *
AVStream_codec_id( py_obj_AVStream *self, void *closure ) {
    return PyLong_FromLong( self->stream->codec->codec_id );
}

static PyObject *
AVStream_codec( py_obj_AVStream *self, void *closure ) {
    if( self->stream->codec->codec_id == CODEC_ID_NONE )
        Py_RETURN_NONE;

    AVCodec *codec = avcodec_find_decoder( self->stream->codec->codec_id );

    if( !codec )
        Py_RETURN_NONE;

    return PyUnicode_DecodeASCII( codec->name, strlen(codec->name), NULL );
}

static PyObject *
AVStream_encoding( py_obj_AVStream *self, void *closure ) {
    if( self->stream->codec->codec_name[0] == '\0' )
        Py_RETURN_NONE;

    return PyUnicode_DecodeASCII( self->stream->codec->codec_name,
        strlen(self->stream->codec->codec_name), NULL );
}

static PyObject *
AVStream_frameCount( py_obj_AVStream *self, void *closure ) {
    if( self->stream->nb_frames <= INT64_C(0) )
        Py_RETURN_NONE;

    return PyLong_FromLongLong( self->stream->nb_frames );
}

static PyObject *
AVStream_startTime( py_obj_AVStream *self, void *closure ) {
    if( self->stream->start_time <= INT64_C(0) )
        Py_RETURN_NONE;

    return PyLong_FromLongLong( self->stream->start_time );
}

static PyObject *
AVStream_duration( py_obj_AVStream *self, void *closure ) {
    if( self->stream->duration <= INT64_C(0) )
        Py_RETURN_NONE;

    return PyLong_FromLongLong( self->stream->duration );
}

static PyGetSetDef AVStream_getsetters[] = {
    { "time_base", (getter) AVStream_timeBase, NULL, "The time base of the stream." },
    { "sample_aspect_ratio", (getter) AVStream_sampleAspectRatio, NULL, "For picture streams, the aspect ratio of each sample, or None if it's unknown." },
    { "pixel_format", (getter) AVStream_pixelFormat, NULL, "Libav's pixel format for this stream, or None if there isn't one." },
    { "type", (getter) AVStream_type, NULL, "The type of stream, one of 'video', 'audio', 'data', 'subtitle', or 'attachment', or None if unknown." },
    { "index", (getter) AVStream_index, NULL, "The index of this stream." },
    { "id", (getter) AVStream_id, NULL, "The format-specific ID of this stream." },
    { "bit_rate", (getter) AVStream_bitRate, NULL, "The bit rate of this stream." },
    { "frame_size", (getter) AVStream_frameSize, NULL, "The size of the frame in the video stream." },
    { "real_frame_rate", (getter) AVStream_realFrameRate, NULL, "A guess at the real frame rate of the stream." },
    { "sample_rate", (getter) AVStream_sampleRate, NULL, "The sample rate in the audio stream." },
    { "channels", (getter) AVStream_channels, NULL, "The number of channels in the audio stream." },
    { "codec", (getter) AVStream_codec, NULL, "The name of the Libav codec that recognizes this stream." },
    { "codec_id", (getter) AVStream_codec_id, NULL, "The ID of the codec." },
    { "encoding", (getter) AVStream_encoding, NULL, "If available, the name of the subformat of this stream." },
    { "frame_count", (getter) AVStream_frameCount, NULL, "If available, the number of frames in this stream." },
    { "start_time", (getter) AVStream_startTime, NULL, "If available, the presentation start time of this stream in time_base units." },
    { "duration", (getter) AVStream_duration, NULL, "If available, the duration of the stream in time_base units." },
    { NULL }
};

static PyTypeObject py_type_AVContainer = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.libav.AVContainer",
    .tp_basicsize = sizeof(py_obj_AVContainer),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AVContainer_dealloc,
    .tp_init = (initproc) AVContainer_init,
    .tp_getset = AVContainer_getsetters,
};

void init_AVContainer( PyObject *module ) {
#define MKCASE(fmt)        pixFmtLookup[PIX_FMT_##fmt] = PyUnicode_FromString( #fmt );
    MKCASE(YUV420P)
    MKCASE(YUYV422)
    MKCASE(RGB24)
    MKCASE(BGR24)
    MKCASE(YUV422P)
    MKCASE(YUV444P)
    MKCASE(YUV410P)
    MKCASE(YUV411P)
    MKCASE(GRAY8)
    MKCASE(MONOWHITE)
    MKCASE(MONOBLACK)
    MKCASE(PAL8)
    MKCASE(YUVJ420P)
    MKCASE(YUVJ422P)
    MKCASE(YUVJ444P)
    MKCASE(XVMC_MPEG2_MC)
    MKCASE(XVMC_MPEG2_IDCT)
    MKCASE(UYVY422)
    MKCASE(UYYVYY411)
    MKCASE(BGR8)
    MKCASE(BGR4)
    MKCASE(BGR4_BYTE)
    MKCASE(RGB8)
    MKCASE(RGB4)
    MKCASE(RGB4_BYTE)
    MKCASE(NV12)
    MKCASE(NV21)

    MKCASE(ARGB)
    MKCASE(RGBA)
    MKCASE(ABGR)
    MKCASE(BGRA)

    MKCASE(GRAY16BE)
    MKCASE(GRAY16LE)
    MKCASE(YUV440P)
    MKCASE(YUVJ440P)
    MKCASE(YUVA420P)
    MKCASE(VDPAU_H264)
    MKCASE(VDPAU_MPEG1)
    MKCASE(VDPAU_MPEG2)
    MKCASE(VDPAU_WMV3)
    MKCASE(VDPAU_VC1)
    MKCASE(RGB48BE)
    MKCASE(RGB48LE)

/*    MKCASE(RGB565BE)
    MKCASE(RGB565LE)
    MKCASE(RGB555BE)
    MKCASE(RGB555LE)

    MKCASE(BGR565BE)
    MKCASE(BGR565LE)
    MKCASE(BGR555BE)
    MKCASE(BGR555LE)*/

    MKCASE(VAAPI_MOCO)
    MKCASE(VAAPI_IDCT)
    MKCASE(VAAPI_VLD)

/*    MKCASE(YUV420PLE)
    MKCASE(YUV420PBE)
    MKCASE(YUV422PLE)
    MKCASE(YUV422PBE)
    MKCASE(YUV444PLE)
    MKCASE(YUV444PBE)*/

    streamTypeLookup[AVMEDIA_TYPE_VIDEO] = PyUnicode_FromString( "video" );
    streamTypeLookup[AVMEDIA_TYPE_AUDIO] = PyUnicode_FromString( "audio" );
    streamTypeLookup[AVMEDIA_TYPE_DATA] = PyUnicode_FromString( "data" );
    streamTypeLookup[AVMEDIA_TYPE_SUBTITLE] = PyUnicode_FromString( "subtitle" );
    streamTypeLookup[AVMEDIA_TYPE_ATTACHMENT] = PyUnicode_FromString( "attachment" );

    py_type_AVStream.tp_getset = AVStream_getsetters;

    if( PyType_Ready( &py_type_AVContainer ) < 0 )
        return;

    if( PyType_Ready( &py_type_AVStream ) < 0 )
        return;

    Py_INCREF( &py_type_AVContainer );
    PyModule_AddObject( module, "AVContainer", (PyObject *) &py_type_AVContainer );

    Py_INCREF( &py_type_AVStream );
    PyModule_AddObject( module, "AVStream", (PyObject *) &py_type_AVStream );
}

