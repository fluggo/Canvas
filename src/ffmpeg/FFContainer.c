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

// Support old FFmpeg
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
} py_obj_FFContainer;

typedef struct {
    PyObject_HEAD

    py_obj_FFContainer *container;
    AVStream *stream;
} py_obj_FFStream;

static int
FFStream_init( py_obj_FFStream *self, PyObject *args, PyObject *kwds ) {
    self->container = NULL;
    self->stream = NULL;

    return 0;
}

static void
FFStream_dealloc( py_obj_FFStream *self ) {
    Py_CLEAR( self->container );
    self->ob_type->tp_free( (PyObject*) self );
}

static PyTypeObject py_type_FFStream = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.ffmpeg.FFStream",    // tp_name
    sizeof(py_obj_FFStream),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) FFStream_dealloc,
    .tp_init = (initproc) FFStream_init,
};

static int
FFContainer_init( py_obj_FFContainer *self, PyObject *args, PyObject *kwds ) {
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

    if( (error = av_find_stream_info( self->format )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Could not find the stream info (%s).", g_strerror( -error ) );
        return -1;
    }

    self->streamList = PyList_New( self->format->nb_streams );

    for( int i = 0; i < self->format->nb_streams; i++ ) {
        py_obj_FFStream *stream = (py_obj_FFStream *) PyObject_CallObject( (PyObject *) &py_type_FFStream, NULL );
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
FFContainer_dealloc( py_obj_FFContainer *self ) {
    Py_CLEAR( self->streamList );

    if( self->format != NULL ) {
        av_close_input_file( self->format );
        self->format = NULL;
    }

    self->ob_type->tp_free( (PyObject*) self );
}

static PyObject *
FFContainer_formatName( py_obj_FFContainer *self, void *closure ) {
    return PyString_FromString( self->format->iformat->name );
}

static PyObject *
FFContainer_formatLongName( py_obj_FFContainer *self, void *closure ) {
    return PyString_FromString( self->format->iformat->long_name );
}

static PyObject *
FFContainer_mimeType( py_obj_FFContainer *self, void *closure ) {
    if( !self->out || !self->out->mime_type )
        Py_RETURN_NONE;

    return PyString_FromString( self->out->mime_type );
}

static PyObject *
FFContainer_bitRate( py_obj_FFContainer *self, void *closure ) {
    return PyInt_FromLong( self->format->bit_rate );
}

static PyObject *
FFContainer_loopCount( py_obj_FFContainer *self, void *closure ) {
    return PyInt_FromLong( self->format->loop_output );
}

static PyObject *
FFContainer_streams( py_obj_FFContainer *self, void *closure ) {
    Py_INCREF( self->streamList );
    return self->streamList;
}

static PyObject *
FFContainer_duration( py_obj_FFContainer *self, void *closure ) {
    return PyLong_FromLongLong( self->format->duration );
}

static PyGetSetDef FFContainer_getsetters[] = {
    { "format_name", (getter) FFContainer_formatName, NULL, "The short name of the container format." },
    { "format_long_name", (getter) FFContainer_formatLongName, NULL, "A more descriptive name of the container format." },
    { "bit_rate", (getter) FFContainer_bitRate, NULL, "The bit rate of the file in bit/s." },
    { "loop_count", (getter) FFContainer_loopCount, NULL, "The number of times the output should loop, or -1 for no looping or 0 for infinite looping." },
    { "streams", (getter) FFContainer_streams, NULL, "List of stream descriptors found in the container." },
    { "mime_type", (getter) FFContainer_mimeType, NULL, "The MIME type of the format, if known." },
    { "duration", (getter) FFContainer_duration, NULL, "The file's (estimated) duration in microseconds." },
    { NULL }
};

static PyObject *
FFStream_timeBase( py_obj_FFStream *self, void *closure ) {
    rational result = { .n = self->stream->time_base.num, .d = self->stream->time_base.den };
    return py_make_rational( &result );
}

static PyObject *
FFStream_realFrameRate( py_obj_FFStream *self, void *closure ) {
    rational result = { .n = self->stream->r_frame_rate.num, .d = self->stream->r_frame_rate.den };

    if( result.n == 0 )
        Py_RETURN_NONE;

    return py_make_rational( &result );
}

static PyObject *
FFStream_sampleAspectRatio( py_obj_FFStream *self, void *closure ) {
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
FFStream_pixelFormat( py_obj_FFStream *self, void *closure ) {
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
FFStream_type( py_obj_FFStream *self, void *closure ) {
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
FFStream_index( py_obj_FFStream *self, void *closure ) {
    return PyInt_FromLong( self->stream->index );
}

static PyObject *
FFStream_id( py_obj_FFStream *self, void *closure ) {
    return PyInt_FromLong( self->stream->id );
}

static PyObject *
FFStream_bitRate( py_obj_FFStream *self, void *closure ) {
    if( self->stream->codec->bit_rate )
        return PyInt_FromLong( self->stream->codec->bit_rate );

    Py_RETURN_NONE;
}

static PyObject *
FFStream_frameSize( py_obj_FFStream *self, void *closure ) {
    if( self->stream->codec->codec_type == AVMEDIA_TYPE_VIDEO )
        return Py_BuildValue( "ii", self->stream->codec->width, self->stream->codec->height );

    Py_RETURN_NONE;
}

static PyObject *
FFStream_sampleRate( py_obj_FFStream *self, void *closure ) {
    if( self->stream->codec->codec_type == AVMEDIA_TYPE_AUDIO )
        return PyInt_FromLong( self->stream->codec->sample_rate );

    Py_RETURN_NONE;
}

static PyObject *
FFStream_channels( py_obj_FFStream *self, void *closure ) {
    if( self->stream->codec->codec_type == AVMEDIA_TYPE_AUDIO )
        return PyInt_FromLong( self->stream->codec->channels );

    Py_RETURN_NONE;
}

static PyObject *
FFStream_codec_id( py_obj_FFStream *self, void *closure ) {
    return PyInt_FromLong( self->stream->codec->codec_id );
}

static PyObject *
FFStream_codec( py_obj_FFStream *self, void *closure ) {
    if( self->stream->codec->codec_id == CODEC_ID_NONE )
        Py_RETURN_NONE;

    AVCodec *codec = avcodec_find_decoder( self->stream->codec->codec_id );

    if( !codec )
        Py_RETURN_NONE;

    return PyString_FromString( codec->name );
}

static PyObject *
FFStream_encoding( py_obj_FFStream *self, void *closure ) {
    if( self->stream->codec->codec_name[0] == '\0' )
        Py_RETURN_NONE;

    return PyString_FromString( self->stream->codec->codec_name );
}

static PyObject *
FFStream_frameCount( py_obj_FFStream *self, void *closure ) {
    if( self->stream->nb_frames <= INT64_C(0) )
        Py_RETURN_NONE;

    return PyLong_FromLongLong( self->stream->nb_frames );
}

static PyObject *
FFStream_startTime( py_obj_FFStream *self, void *closure ) {
    if( self->stream->start_time <= INT64_C(0) )
        Py_RETURN_NONE;

    return PyLong_FromLongLong( self->stream->start_time );
}

static PyObject *
FFStream_duration( py_obj_FFStream *self, void *closure ) {
    if( self->stream->duration <= INT64_C(0) )
        Py_RETURN_NONE;

    return PyLong_FromLongLong( self->stream->duration );
}

static PyGetSetDef FFStream_getsetters[] = {
    { "time_base", (getter) FFStream_timeBase, NULL, "The time base of the stream." },
    { "sample_aspect_ratio", (getter) FFStream_sampleAspectRatio, NULL, "For picture streams, the aspect ratio of each sample, or None if it's unknown." },
    { "pixel_format", (getter) FFStream_pixelFormat, NULL, "FFmpeg's pixel format for this stream, or None if there isn't one." },
    { "type", (getter) FFStream_type, NULL, "The type of stream, one of 'video', 'audio', 'data', 'subtitle', or 'attachment', or None if unknown." },
    { "index", (getter) FFStream_index, NULL, "The index of this stream." },
    { "id", (getter) FFStream_id, NULL, "The format-specific ID of this stream." },
    { "bit_rate", (getter) FFStream_bitRate, NULL, "The bit rate of this stream." },
    { "frame_size", (getter) FFStream_frameSize, NULL, "The size of the frame in the video stream." },
    { "real_frame_rate", (getter) FFStream_realFrameRate, NULL, "A guess at the real frame rate of the stream." },
    { "sample_rate", (getter) FFStream_sampleRate, NULL, "The sample rate in the audio stream." },
    { "channels", (getter) FFStream_channels, NULL, "The number of channels in the audio stream." },
    { "codec", (getter) FFStream_codec, NULL, "The name of the FFmpeg codec that recognizes this stream." },
    { "codec_id", (getter) FFStream_codec_id, NULL, "The ID of the codec." },
    { "encoding", (getter) FFStream_encoding, NULL, "If available, the name of the subformat of this stream." },
    { "frame_count", (getter) FFStream_frameCount, NULL, "If available, the number of frames in this stream." },
    { "start_time", (getter) FFStream_startTime, NULL, "If available, the presentation start time of this stream in timeBase units." },
    { "duration", (getter) FFStream_duration, NULL, "If available, the duration of the stream in timeBase units." },
    { NULL }
};

static PyTypeObject py_type_FFContainer = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.ffmpeg.FFContainer",    // tp_name
    sizeof(py_obj_FFContainer),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) FFContainer_dealloc,
    .tp_init = (initproc) FFContainer_init,
    .tp_getset = FFContainer_getsetters,
};

void init_FFContainer( PyObject *module ) {
#define MKCASE(fmt)        pixFmtLookup[PIX_FMT_##fmt] = PyString_FromString( #fmt );
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

    streamTypeLookup[AVMEDIA_TYPE_VIDEO] = PyString_FromString( "video" );
    streamTypeLookup[AVMEDIA_TYPE_AUDIO] = PyString_FromString( "audio" );
    streamTypeLookup[AVMEDIA_TYPE_DATA] = PyString_FromString( "data" );
    streamTypeLookup[AVMEDIA_TYPE_SUBTITLE] = PyString_FromString( "subtitle" );
    streamTypeLookup[AVMEDIA_TYPE_ATTACHMENT] = PyString_FromString( "attachment" );

    py_type_FFStream.tp_getset = FFStream_getsetters;

    if( PyType_Ready( &py_type_FFContainer ) < 0 )
        return;

    if( PyType_Ready( &py_type_FFStream ) < 0 )
        return;

    Py_INCREF( &py_type_FFContainer );
    PyModule_AddObject( module, "FFContainer", (PyObject *) &py_type_FFContainer );

    Py_INCREF( &py_type_FFStream );
    PyModule_AddObject( module, "FFStream", (PyObject *) &py_type_FFStream );
}

