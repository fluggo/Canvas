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

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "fluggo.media.faac.AVAudioDecoder"

typedef struct {
    int64_t timestamp;
    int64_t sample;
    int duration;
} timestamp_to_sample;

static void
destroy_tts( timestamp_to_sample *ptr ) {
    g_slice_free( timestamp_to_sample, ptr );
}

G_GNUC_PURE static gint
compare_tts( timestamp_to_sample *a, timestamp_to_sample *b, gpointer user_data ) {
    return a->timestamp - b->timestamp;
}

typedef struct {
    PyObject_HEAD

    CodecPacketSourceHolder source;
    AVCodecContext context;
    AVFrame *input_frame;
    void *audio_buffer;
    int last_packet_start, last_packet_duration;
    bool context_open, trust_timestamps;
    GSequence *timestamp_to_sample;
    int32_t max_sample_seen;

    int64_t current_pts;
    bool current_pts_valid;

    GStaticMutex mutex;
} py_obj_AVAudioDecoder;

static int
AVAudioDecoder_init( py_obj_AVAudioDecoder *self, PyObject *args, PyObject *kw ) {
    int error;

    // Zero all pointers (so we know later what needs deleting)
    self->input_frame = NULL;
    self->timestamp_to_sample = NULL;
    self->trust_timestamps = false;
    self->audio_buffer = NULL;
    self->max_sample_seen = INT32_MIN;
    self->current_pts_valid = false;

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

    if( !self->trust_timestamps ) {
        self->timestamp_to_sample = g_sequence_new( (GDestroyNotify) destroy_tts );
    }

    return 0;
}

static void
AVAudioDecoder_dealloc( py_obj_AVAudioDecoder *self ) {
    PyMem_Free( self->audio_buffer );
    self->audio_buffer = NULL;

    if( self->context_open )
        avcodec_close( &self->context );

    if( self->timestamp_to_sample )
        g_sequence_free( self->timestamp_to_sample );

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

    // BJC: What does trust_timestamps mean? Well, some containers have timestamps
    // that are less precise than their sample rate. DV, for example, has timestamps
    // with a resolution of 1/30000, but a sample rate of 48000 Hz. The packet
    // source will do its best to convert the timestamps into samples, but it
    // does not have enough information to be accurate to the sample.
    //
    // That's fine for pure playback scenarios. We've got to be able to produce
    // frames from any point in the stream accurate to the sample. So, by default,
    // unless someone gives us permission to trust that the timestamps are accurate
    // with trust_timestamps, we make sure to decode our way through the stream
    // and build a map of what timestamps correspond to what actual samples.

    bool do_seek = false;

    if( self->source.source.funcs->seek ) {
        do_seek = true;

        if( self->current_pts_valid ) {
            // If our last packet was before (but not too far before) the current
            // frame, read up to it
            do_seek = frame->full_min_sample < self->current_pts ||
                frame->full_min_sample >= (self->current_pts + 10000);

            // But don't do it if our last packet contains the seek point
            if( self->last_packet_duration != 0 &&
                    self->last_packet_start + self->last_packet_duration == self->current_pts &&
                    frame->full_min_sample >= self->last_packet_start &&
                    frame->full_min_sample <= self->current_pts )
                do_seek = false;

            if( do_seek )
                g_debug( "Deciding to seek, current %" PRId64 " vs. target %d", self->current_pts, frame->full_min_sample );
        }

        if( !self->trust_timestamps && frame->full_min_sample < self->max_sample_seen )
            do_seek = false;

        if( do_seek && !self->source.source.funcs->seek( self->source.source.obj, frame->full_min_sample ) ) {
            g_warning( "Failed to seek to sample %d", frame->full_min_sample );
            g_static_mutex_unlock( &self->mutex );
            frame->current_max_sample = -1;
            frame->current_min_sample = 0;
            return;
        }
    }

    bool first = true;

    if( self->last_packet_duration != 0 && self->last_packet_start <= frame->full_max_sample &&
        (self->last_packet_start + self->last_packet_duration) >= frame->full_min_sample ) {

        // Decode into the current frame
        int start_sample = max(self->last_packet_start, frame->full_min_sample);
        int duration = min(self->last_packet_start + self->last_packet_duration, frame->full_max_sample + 1) - start_sample;
        float *out = audio_get_sample( frame, start_sample, 0 );

        g_debug( "Using last packet from (%d->%d, %d->%d)",
            self->last_packet_start, start_sample,
            self->last_packet_start + self->last_packet_duration - 1,
            start_sample + duration - 1 );

        convert_samples( out, frame->channels, self->audio_buffer, self->context.channels, (start_sample - self->last_packet_start),
            self->context.sample_fmt, duration );

        frame->current_min_sample = start_sample;
        frame->current_max_sample = start_sample + duration - 1;
        first = false;

        // We could be done...
        if( frame->current_min_sample == frame->full_min_sample && frame->current_max_sample == frame->full_max_sample ) {
            g_debug( "Going home early: (%d, %d)", start_sample, duration );
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

        while( data_size > 0 ) {
            int decoded;

            //printf( "Decoding audio (size left: %d)\n", dataSize );
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 23, 0)
            if( (decoded = avcodec_decode_audio2( &self->context, audio_buffer, &buffer_size, data, data_size )) < 0 ) {
#else
            if( (decoded = avcodec_decode_audio3( &self->context, audio_buffer, &buffer_size, &av_packet )) < 0 ) {
#endif
                g_warning( "Could not decode the audio (%s).", g_strerror( -decoded ) );
                frame->current_max_sample = -1;
                frame->current_min_sample = 0;

                if( packet && packet->free_func )
                    packet->free_func( packet );

                g_static_mutex_unlock( &self->mutex );
                return;
            }

            if( buffer_size <= 0 ) {
                g_warning( "Didn't get a sound" );
                continue;
            }

            //g_debug( "Decoded %d bytes, got %d bytes", decoded, buffer_size );

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 23, 0)
            data += decoded;
#endif
            data_size -= decoded;
            audio_buffer += buffer_size;
            buffer_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;        // A lie, but a safe one, I think
        }

        // Patch up timestamps if necessary
        int packet_start = packet->pts;
        int packet_duration = packet->duration;

        g_debug( "Got packet start %d duration %d", packet_start, packet_duration );

        if( !self->trust_timestamps ) {
            // Calculate new duration
            packet_duration = get_sample_count( audio_buffer - self->audio_buffer,
                self->context.sample_fmt, self->context.channels );

            if( !do_seek && self->current_pts_valid ) {
                // We can trust our previous PTS
                packet_start = self->current_pts;
            }
            else {
                // We will use the PTS from the packet;
                // the idea is that this will only really stick for the first
                // packet, since on the next pass, we will override it.
            }

            g_debug( "Substituted start %d duration %d", packet_start, packet_duration );

            // Look up entry in our table
            timestamp_to_sample tts = {
                .timestamp = packet->pts,
                .sample = packet_start,
                .duration = packet_duration };

            GSequenceIter *iter = g_sequence_lookup( self->timestamp_to_sample,
                &tts, (GCompareDataFunc) compare_tts, NULL );

            if( iter == NULL ) {
                // We don't have this entry, make one
                if( do_seek )
                    g_warning( "Seek brought us to a point not covered in the timestamp table." );

                g_debug( "Adding to timestamp table" );

                timestamp_to_sample *ttsptr = g_slice_dup( timestamp_to_sample, &tts );

                g_sequence_insert_sorted( self->timestamp_to_sample,
                    ttsptr, (GCompareDataFunc) compare_tts, NULL );
            }
            else {
                // This packet is covered, let's use the start and duration we recorded
                timestamp_to_sample *ttsptr = g_sequence_get( iter );

                packet_start = ttsptr->sample;
                packet_duration = ttsptr->duration;

                g_debug( "Re-substituting start %d duration %d from timestamp table", packet_start, packet_duration );
            }
        }

        // After this, we haven't seeked to this spot anymore
        do_seek = false;

        self->last_packet_start = packet_start;
        self->last_packet_duration = packet_duration;
        self->current_pts = packet_start + packet_duration;
        self->current_pts_valid = true;
        self->max_sample_seen = packet_start + packet_duration - 1;

        g_debug( "We'll take that (%d, %d)", packet_start, packet_start + packet_duration - 1 );
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

            g_debug( "Enough: (%d, %d) vs (%d, %d)", frame->current_min_sample, frame->current_max_sample, frame->full_min_sample, frame->full_max_sample );
            g_static_mutex_unlock( &self->mutex );
            return;
        }
        else {
            g_debug( "Not enough: (%d, %d) vs (%d, %d)", frame->current_min_sample, frame->current_max_sample, frame->full_min_sample, frame->full_max_sample );
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



