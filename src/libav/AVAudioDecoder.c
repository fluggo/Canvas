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

typedef struct {
    PyObject_HEAD

    CodecPacketSourceHolder source;
    AVCodecContext context;
    AVFrame *input_frame;
    void *audio_buffer;
    int last_packet_start, last_packet_duration;
    bool context_open;

    GStaticMutex mutex;
} py_obj_AVAudioDecoder;

static int
AVAudioDecoder_init( py_obj_AVAudioDecoder *self, PyObject *args, PyObject *kw ) {
    int error;

    // Zero all pointers (so we know later what needs deleting)
    self->input_frame = NULL;

    PyObject *source_obj;
    const char *codec_name;
    int channels;

    static char *kwlist[] = { "source", "codec", "channels", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "Osi", kwlist, &source_obj, &codec_name, &channels ) )
        return -1;

    avcodec_register_all();
    AVCodec *codec = avcodec_find_decoder_by_name( codec_name );

    if( !codec ) {
        PyErr_Format( PyExc_Exception, "Could not find the codec \"%s\".", codec_name );
        return -1;
    }

    if( !py_codec_packet_take_source( source_obj, &self->source ) )
        return -1;

    avcodec_get_context_defaults( &self->context );
    self->context.channels = channels;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 6, 0)
    if( (error = avcodec_open( &self->context, codec )) != 0 ) {
#else
    if( (error = avcodec_open2( &self->context, codec, NULL )) != 0 ) {
#endif
        PyErr_Format( PyExc_Exception, "Could not open the codec (%s).", g_strerror( -error ) );
        py_codec_packet_take_source( NULL, &self->source );
        return -1;
    }

    self->context_open = true;
    g_static_mutex_init( &self->mutex );

    self->audio_buffer = PyMem_Malloc( AVCODEC_MAX_AUDIO_FRAME_SIZE );

    if( !self->audio_buffer ) {
        PyErr_NoMemory();
        return -1;
    }

    self->last_packet_start = 0;
    self->last_packet_duration = 0;

    return 0;
}

static void
AVAudioDecoder_dealloc( py_obj_AVAudioDecoder *self ) {
    PyMem_Free( self->audio_buffer );
    self->audio_buffer = NULL;

    if( self->context_open )
        avcodec_close( &self->context );

    py_codec_packet_take_source( NULL, &self->source );
    g_static_mutex_free( &self->mutex );

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static void convert_samples( float *out, int out_channels, void *in, int in_channels, int offset, enum SampleFormat sample_fmt, int duration ) {
    #define CONVERT(in_type, factor) \
        for( int sample = 0; sample < duration; sample++ ) { \
            for( int channel = 0; channel < out_channels; channel++ ) { \
                out[out_channels * sample + channel] = \
                    (channel < in_channels) ? (float)((in_type *) in)[in_channels * (offset + sample) + channel] * factor : 0.0f; \
            } \
        }

    switch( sample_fmt ) {
        case SAMPLE_FMT_U8:
            CONVERT(uint8_t, (1.0f / (float) INT8_MAX) - 1.0f)
            return;

        case SAMPLE_FMT_S16:
            CONVERT(int16_t, (1.0f / (float) INT16_MAX))
            return;

        case SAMPLE_FMT_S32:
            CONVERT(int32_t, (1.0f / (float) INT32_MAX))
            return;

        case SAMPLE_FMT_FLT:
            CONVERT(float, 1.0f)
            return;

        case SAMPLE_FMT_DBL:
            CONVERT(double, 1.0f)
            return;

        default:
            printf( "Unknown sample type.\n" );
            return;
    }
}

static int get_sample_count( int bytes, enum SampleFormat sample_fmt, int channels ) {
    static int format_size[] = { 1, 2, 4, 4, 8 };

    if( sample_fmt < 0 || sample_fmt > 4 )
        sample_fmt = 1;

    return bytes / (format_size[sample_fmt] * channels);
}

static void
AVAudioDecoder_get_frame( py_obj_AVAudioDecoder *self, audio_frame *frame ) {
    g_static_mutex_lock( &self->mutex );

    if( self->source.source.funcs->seek &&
            self->last_packet_start != -1 &&
            (frame->full_min_sample < self->last_packet_start ||
                (frame->full_min_sample - self->last_packet_start) >= (frame->full_max_sample - frame->full_min_sample + 1) * 4) ) {

        if( !self->source.source.funcs->seek( self->source.source.obj, frame->full_min_sample ) ) {
            g_static_mutex_unlock( &self->mutex );
            frame->current_max_sample = -1;
            frame->current_min_sample = 0;
            return;
        }
    }

    bool first = true;

    if( self->last_packet_start != -1 && self->last_packet_start <= frame->full_max_sample &&
        (self->last_packet_start + self->last_packet_duration) >= frame->full_min_sample ) {

        // Decode into the current frame
        int start_sample = max(self->last_packet_start, frame->full_min_sample);
        int duration = min(self->last_packet_start + self->last_packet_duration, frame->full_max_sample + 1) - start_sample;
        float *out = audio_get_sample( frame, start_sample, 0 );

        convert_samples( out, frame->channels, self->audio_buffer, self->context.channels, (start_sample - self->last_packet_start),
            self->context.sample_fmt, duration );

        frame->current_min_sample = start_sample;
        frame->current_max_sample = start_sample + duration - 1;
        first = false;

        // We could be done...
        if( frame->current_min_sample == frame->full_min_sample && frame->current_max_sample == frame->full_max_sample ) {
            //printf( "Going home early: (%d, %d)\n", startSample, duration );
            g_static_mutex_unlock( &self->mutex );
            return;
        }
    }

    codec_packet *packet = NULL;

    for( ;; ) {
        if( packet && packet->free_func )
            packet->free_func( packet );

        packet = self->source.source.funcs->getNextPacket( self->source.source.obj );

        if( !packet ) {
            g_static_mutex_unlock( &self->mutex );
            return;
        }

        int buffer_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 23, 0)
        uint8_t *data = packet->data;
#else
        AVPacket av_packet = {
            .pts = AV_NOPTS_VALUE,
            .dts = AV_NOPTS_VALUE,
            .data = packet->data,
            .size = packet->length };
#endif
        int data_size = packet->length;

        void *audio_buffer = self->audio_buffer;

        int packet_start = packet->pts;
        int packet_duration = 0;

        while( data_size > 0 ) {
            int decoded;

            //printf( "Decoding audio (size left: %d)\n", dataSize );
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 23, 0)
            if( (decoded = avcodec_decode_audio2( &self->context, audio_buffer, &buffer_size, data, data_size )) < 0 ) {
#else
            if( (decoded = avcodec_decode_audio3( &self->context, audio_buffer, &buffer_size, &av_packet )) < 0 ) {
#endif
                printf( "Could not decode the audio (%s).\n", g_strerror( -decoded ) );
                frame->current_max_sample = -1;
                frame->current_min_sample = 0;

                if( packet && packet->free_func )
                    packet->free_func( packet );

                g_static_mutex_unlock( &self->mutex );
                return;
            }

            if( buffer_size <= 0 ) {
                //printf( "Didn't get a sound\n" );
                continue;
            }

            //printf( "Decoded %d bytes, got %d bytes\n", decoded, bufferSize );

            packet_duration += get_sample_count( buffer_size, self->context.sample_fmt, self->context.channels );

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 23, 0)
            data += decoded;
#endif
            data_size -= decoded;
            audio_buffer += buffer_size;
            buffer_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;        // A lie, but a safe one, I think
        }

        self->last_packet_start = packet_start;
        self->last_packet_duration = packet_duration;

        //printf( "We'll take that (%d, %d)\n", packetStart, packetDuration );
        int start_sample = max(packet_start, frame->full_min_sample);
        int duration = min(packet_start + packet_duration, frame->full_max_sample + 1) - start_sample;
        float *out = audio_get_sample( frame, start_sample, 0 );

        convert_samples( out, frame->channels, self->audio_buffer, self->context.channels, (start_sample - packet_start),
            self->context.sample_fmt, duration );

        if( first ) {
            frame->current_min_sample = packet_start;
            frame->current_max_sample = packet_start + packet_duration - 1;
            first = false;
        }
        else {
            frame->current_min_sample = min(frame->current_min_sample, packet_start);
            frame->current_max_sample = max(frame->current_max_sample, packet_start + packet_duration - 1);
        }

        frame->current_min_sample = max(frame->current_min_sample, frame->full_min_sample);
        frame->current_max_sample = min(frame->current_max_sample, frame->full_max_sample);

        if( packet_start + packet_duration >= frame->full_max_sample ) {
            if( packet && packet->free_func )
                packet->free_func( packet );

//            printf( "Enough: (%d, %d) vs (%d, %d)\n", frame->current_min_sample, frame->current_max_sample, frame->full_min_sample, frame->full_max_sample );
            g_static_mutex_unlock( &self->mutex );
            return;
        }
        else {
//            printf( "Not enough: (%d, %d) vs (%d, %d)\n", frame->currentMinSample, frame->currentMaxSample, frame->fullMinSample, frame->fullMaxSample );
        }
    }
}

static AudioFrameSourceFuncs source_funcs = {
    .getFrame = (audio_getFrameFunc) AVAudioDecoder_get_frame,
};

static PyObject *pySourceFuncs;

static PyObject *
AVAudioDecoder_getFuncs( py_obj_AVAudioDecoder *self, void *closure ) {
    Py_INCREF(pySourceFuncs);
    return pySourceFuncs;
}

static PyGetSetDef AVAudioDecoder_getsetters[] = {
    { AUDIO_FRAME_SOURCE_FUNCS, (getter) AVAudioDecoder_getFuncs, NULL, "Audio frame source C API." },
    { NULL }
};

static PyMethodDef AVAudioDecoder_methods[] = {
    { NULL }
};

static PyTypeObject py_type_AVAudioDecoder = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.libav.AVAudioDecoder",
    .tp_basicsize = sizeof(py_obj_AVAudioDecoder),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_AudioSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AVAudioDecoder_dealloc,
    .tp_init = (initproc) AVAudioDecoder_init,
    .tp_getset = AVAudioDecoder_getsetters,
    .tp_methods = AVAudioDecoder_methods
};

void init_AVAudioDecoder( PyObject *module ) {
    if( PyType_Ready( &py_type_AVAudioDecoder ) < 0 )
        return;

    Py_INCREF( &py_type_AVAudioDecoder );
    PyModule_AddObject( module, "AVAudioDecoder", (PyObject *) &py_type_AVAudioDecoder );

    pySourceFuncs = PyCapsule_New( &source_funcs, AUDIO_FRAME_SOURCE_FUNCS, NULL );
}



