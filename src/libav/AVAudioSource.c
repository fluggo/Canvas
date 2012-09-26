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
#define AVMEDIA_TYPE_AUDIO      CODEC_TYPE_AUDIO
#endif

#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(50, 32, 0)
#define AVSampleFormat SampleFormat
#define AV_SAMPLE_FMT_U8  SAMPLE_FMT_U8
#define AV_SAMPLE_FMT_S16 SAMPLE_FMT_S16
#define AV_SAMPLE_FMT_S32 SAMPLE_FMT_S32
#define AV_SAMPLE_FMT_FLT SAMPLE_FMT_FLT
#define AV_SAMPLE_FMT_DBL SAMPLE_FMT_DBL
#endif

/******** AVAudioSource *********/
typedef struct {
    PyObject_HEAD

    AVFormatContext *context;
    AVCodecContext *codecContext;
    AVCodec *codec;
    int firstAudioStream;
    AVFrame *inputFrame;
    bool allKeyframes;
    void *audioBuffer;
    int lastPacketStart, lastPacketDuration;
} py_obj_AVAudioSource;

static int
AVAudioSource_init( py_obj_AVAudioSource *self, PyObject *args, PyObject *kwds ) {
    int error;
    char *filename;

    // Zero all pointers (so we know later what needs deleting)
    self->context = NULL;
    self->codecContext = NULL;
    self->codec = NULL;
    self->inputFrame = NULL;

    if( !PyArg_ParseTuple( args, "s", &filename ) )
        return -1;

    av_register_all();

#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 2, 0)
    if( (error = av_open_input_file( &self->context, filename, NULL, 0, NULL )) != 0 ) {
#else
    if( (error = avformat_open_input( &self->context, filename, NULL, NULL )) != 0 ) {
#endif
        PyErr_Format( PyExc_Exception, "Could not open the file (%s).", g_strerror( -error ) );
        return -1;
    }

    if( (error = av_find_stream_info( self->context )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Could not find the stream info (%s).", g_strerror( -error ) );
        return -1;
    }

    for( int i = 0; i < self->context->nb_streams; i++ ) {
        if( self->context->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO ) {
            self->firstAudioStream = i;
            break;
        }
    }

    if( self->firstAudioStream == -1 ) {
        PyErr_SetString( PyExc_Exception, "Could not find an audio stream." );
        return -1;
    }

    self->codecContext = self->context->streams[self->firstAudioStream]->codec;
    self->codec = avcodec_find_decoder( self->codecContext->codec_id );

    if( self->codec == NULL ) {
        PyErr_SetString( PyExc_Exception, "Could not find a codec for the stream." );
        return -1;
    }

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 6, 0)
    if( (error = avcodec_open( self->codecContext, self->codec )) < 0 ) {
#else
    if( (error = avcodec_open2( self->codecContext, self->codec, NULL )) < 0 ) {
#endif
        PyErr_Format( PyExc_Exception, "Could not open a codec (%s).", g_strerror( -error ) );
        return -1;
    }

    self->audioBuffer = PyMem_Malloc( AVCODEC_MAX_AUDIO_FRAME_SIZE );

    if( !self->audioBuffer ) {
        PyErr_NoMemory();
        return -1;
    }

    // Use MLT's keyframe conditions
/*    self->allKeyframes = !(strcmp( self->codecContext->codec->name, "mjpeg" ) &&
      strcmp( self->codecContext->codec->name, "rawvideo" ) &&
      strcmp( self->codecContext->codec->name, "dvvideo" ));

    if( !self->allKeyframes ) {
        // Prime the pump for MPEG so we get frame accuracy (otherwise we seem to start a few frames in)
        AVPacket packet;
        int gotPicture;

        av_init_packet( &packet );
        av_read_frame( self->context, &packet );

        if( packet.stream_index == self->firstVideoStream ) {
            avcodec_decode_video( self->codecContext, self->inputFrame, &gotPicture,
                packet.data, packet.size );
        }

        av_free_packet( &packet );
        av_seek_frame( self->context, self->firstVideoStream, 0, AVSEEK_FLAG_BACKWARD );
    }*/

    self->lastPacketStart = 0;
    self->lastPacketDuration = 0;

    return 0;
}

static void
AVAudioSource_dealloc( py_obj_AVAudioSource *self ) {
    PyMem_Free( self->audioBuffer );
    self->audioBuffer = NULL;

    if( self->codecContext != NULL ) {
        avcodec_close( self->codecContext );
        self->codecContext = NULL;
    }

    if( self->context != NULL ) {
        av_close_input_file( self->context );
        self->context = NULL;
    }

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static void convert_uint8( float *out, int outChannels, uint8_t *in, int inChannels, int duration ) {
    int channelCount = min(outChannels, inChannels);

    for( int sample = 0; sample < duration; sample++ ) {
        for( int channel = 0; channel < channelCount; channel++ ) {
            out[outChannels * sample + channel] = (float) in[inChannels * sample + channel] * (1.0f / (float) INT8_MAX) - 1.0f;
        }
    }
}

static void convert_int16( float *out, int outChannels, int16_t *in, int inChannels, int duration ) {
    int channelCount = min(outChannels, inChannels);

    for( int sample = 0; sample < duration; sample++ ) {
        for( int channel = 0; channel < channelCount; channel++ ) {
            out[outChannels * sample + channel] = (float) in[inChannels * sample + channel] * (1.0f / (float) INT16_MAX);
        }
    }
}

static void convert_int32( float *out, int outChannels, int32_t *in, int inChannels, int duration ) {
    int channelCount = min(outChannels, inChannels);

    for( int sample = 0; sample < duration; sample++ ) {
        for( int channel = 0; channel < channelCount; channel++ ) {
            out[outChannels * sample + channel] = (float) in[inChannels * sample + channel] * (1.0f / (float) INT32_MAX);
        }
    }
}

static void convert_float( float *out, int outChannels, float *in, int inChannels, int duration ) {
    int channelCount = min(outChannels, inChannels);

    for( int sample = 0; sample < duration; sample++ ) {
        for( int channel = 0; channel < channelCount; channel++ ) {
            out[outChannels * sample + channel] = in[inChannels * sample + channel];
        }
    }
}

static void convert_double( float *out, int outChannels, double *in, int inChannels, int duration ) {
    int channelCount = min(outChannels, inChannels);

    for( int sample = 0; sample < duration; sample++ ) {
        for( int channel = 0; channel < channelCount; channel++ ) {
            out[outChannels * sample + channel] = (float) in[inChannels * sample + channel];
        }
    }
}

static void convert_samples( float *out, int outChannels, void *in, int inChannels, int offset, enum AVSampleFormat sample_fmt, int duration ) {
    switch( sample_fmt ) {
        case AV_SAMPLE_FMT_U8:
            convert_uint8( out, outChannels,
                ((uint8_t *) in) + offset * inChannels, inChannels, duration );
            return;

        case AV_SAMPLE_FMT_S16:
            convert_int16( out, outChannels,
                ((int16_t *) in) + offset * inChannels, inChannels, duration );
            return;

        case AV_SAMPLE_FMT_S32:
            convert_int32( out, outChannels,
                ((int32_t *) in) + offset * inChannels, inChannels, duration );
            return;

        case AV_SAMPLE_FMT_FLT:
            convert_float( out, outChannels,
                ((float *) in) + offset * inChannels, inChannels, duration );
            return;

        case AV_SAMPLE_FMT_DBL:
            convert_double( out, outChannels,
                ((double *) in) + offset * inChannels, inChannels, duration );
            return;

        default:
            printf( "Unknown sample type.\n" );
            return;
    }
}

static int getSampleCount( int byteCount, enum AVSampleFormat sample_fmt, int channels ) {
    static int formatSize[] = { 1, 2, 4, 4, 8 };

    if( sample_fmt < 0 || sample_fmt > 4 )
        sample_fmt = 1;

    return byteCount / (formatSize[sample_fmt] * channels);
}

static void
AVAudioSource_getFrame( py_obj_AVAudioSource *self, audio_frame *frame ) {
/*    if( frameIndex < 0 || frameIndex > self->context->streams[self->firstAudioStream]->duration ) {
        // No result
        frame->currentMaxSample = -1;
        frame->currentMinSample = 0;
        return;
    }*/

    //printf( "Requested %ld\n", frameIndex );

    AVRational *timeBase = &self->context->streams[self->firstAudioStream]->time_base;
    int sampleRate = self->codecContext->sample_rate;
    //printf( "timeBase: %d/%d, frameRate: %d/%d\n", timeBase->num, timeBase->den, sampleRate, 1 );
    int64_t frameDuration = (timeBase->den) / (timeBase->num * sampleRate);
    int64_t timestamp = ((int64_t) frame->full_min_sample * timeBase->den) / (timeBase->num * sampleRate) + frameDuration / 2;


//    if( (uint64_t) self->context->start_time != AV_NOPTS_VALUE )
//        timestamp += self->context->start_time;

/*    if( self->allKeyframes ) {
        if( av_seek_frame( self->context, self->firstAudioStream, frame->full_min_sample,
                AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD ) < 0 )
            printf( "Could not seek to frame %ld.\n", frame->full_min_sample );

        self->currentAudioFrame = frame->full_min_sample;
    }
    else {*/
        // Only bother seeking if we're way off (or it's behind us)
        if( self->lastPacketStart != -1 && (frame->full_min_sample < self->lastPacketStart || (frame->full_min_sample - self->lastPacketStart) >= (frame->full_max_sample - frame->full_min_sample) * 4) ) {

            //printf( "min: %d, lastPacket: %d\n", frame->full_min_sample, self->lastPacketStart );
            //printf( "Seeking back to %ld...\n", timestamp );
            int seekStamp = timestamp;

            if( seekStamp < 0 )
                seekStamp = 0;

            av_seek_frame( self->context, self->firstAudioStream, seekStamp, AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD );
        }
//    }

    bool first = true;

    if( self->lastPacketStart != -1 && self->lastPacketStart <= frame->full_max_sample &&
        (self->lastPacketStart + self->lastPacketDuration) >= frame->full_min_sample ) {

        // Decode into the current frame
        int startSample = max(self->lastPacketStart, frame->full_min_sample);
        int duration = min(self->lastPacketStart + self->lastPacketDuration, frame->full_max_sample + 1) - startSample;
        float *out = audio_get_sample( frame, startSample, 0 );

        convert_samples( out, frame->channels, self->audioBuffer, self->codecContext->channels, (startSample - self->lastPacketStart),
            self->codecContext->sample_fmt, duration );

        frame->current_min_sample = startSample;
        frame->current_max_sample = startSample + duration - 1;
        first = false;

        // We could be done...
        if( frame->current_min_sample == frame->full_min_sample && frame->current_max_sample == frame->full_max_sample ) {
            //printf( "Going home early: (%d, %d)\n", startSample, duration );
            return;
        }
    }

    for( ;; ) {
        AVPacket packet;
        av_init_packet( &packet );

        //printf( "Reading frame\n" );
        if( av_read_frame( self->context, &packet ) < 0 ) {
            printf( "Could not read the frame.\n" );
            return;
        }

        if( packet.stream_index != self->firstAudioStream ) {
            //printf( "Not the right stream\n" );
            av_free_packet( &packet );
            continue;
        }

        if( (packet.dts + packet.duration) < timestamp ) {
            //printf( "Too early (%lld + %d vs %lld)\n", packet.dts, packet.duration, timestamp );
            av_free_packet( &packet );
            continue;
        }

        int bufferSize = AVCODEC_MAX_AUDIO_FRAME_SIZE;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 23, 0)
        uint8_t *data = packet.data;
#endif
        int dataSize = packet.size;
        void *audioBuffer = self->audioBuffer;

        int packetStart = (packet.dts * timeBase->num * sampleRate) / (timeBase->den);
        int packetDuration = 0;

        while( dataSize > 0 ) {
            int decoded;

            //printf( "Decoding audio (size left: %d)\n", dataSize );
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 23, 0)
            if( (decoded = avcodec_decode_audio2( self->codecContext, audioBuffer, &bufferSize,
                data, dataSize )) < 0 ) {
#else
            if( (decoded = avcodec_decode_audio3( self->codecContext, audioBuffer, &bufferSize,
                &packet )) < 0 ) {
#endif
                printf( "Could not decode the audio.\n" );
                frame->current_max_sample = -1;
                frame->current_min_sample = 0;
                av_free_packet( &packet );
                return;
            }

            if( bufferSize <= 0 ) {
                //printf( "Didn't get a sound\n" );
                av_free_packet( &packet );
                continue;
            }

            //printf( "Decoded %d bytes, got %d bytes\n", decoded, bufferSize );

            packetDuration += getSampleCount( bufferSize, self->codecContext->sample_fmt, self->codecContext->channels );

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 23, 0)
            data += decoded;
#endif
            dataSize -= decoded;
            audioBuffer += bufferSize;
            bufferSize = AVCODEC_MAX_AUDIO_FRAME_SIZE;        // A lie, but a safe one, I think
        }

        self->lastPacketStart = packetStart;
        self->lastPacketDuration = packetDuration;

        //printf( "We'll take that (%d, %d)\n", packetStart, packetDuration );
        int startSample = max(packetStart, frame->full_min_sample);
        int duration = min(packetStart + packetDuration, frame->full_max_sample + 1) - startSample;
        float *out = audio_get_sample( frame, startSample, 0 );

        convert_samples( out, frame->channels, self->audioBuffer, self->codecContext->channels, (startSample - packetStart),
            self->codecContext->sample_fmt, duration );

        if( first ) {
            frame->current_min_sample = packetStart;
            frame->current_max_sample = packetStart + packetDuration - 1;
            first = false;
        }
        else {
            frame->current_min_sample = min(frame->current_min_sample, packetStart);
            frame->current_max_sample = max(frame->current_max_sample, packetStart + packetDuration - 1);
        }

        frame->current_min_sample = max(frame->current_min_sample, frame->full_min_sample);
        frame->current_max_sample = min(frame->current_max_sample, frame->full_max_sample);

        av_free_packet( &packet );

        if( packetStart + packetDuration >= frame->full_max_sample ) {
//            printf( "Enough: (%d, %d) vs (%d, %d)\n", frame->currentMinSample, frame->currentMaxSample, frame->fullMinSample, frame->fullMaxSample );
            return;
        }
        else {
//            printf( "Not enough: (%d, %d) vs (%d, %d)\n", frame->currentMinSample, frame->currentMaxSample, frame->fullMinSample, frame->fullMaxSample );
        }
    }
}

static AudioFrameSourceFuncs audioSourceFuncs = {
    0,
    (audio_getFrameFunc) AVAudioSource_getFrame
};

static PyObject *pyAudioSourceFuncs;

static PyObject *
AVAudioSource_getFuncs( py_obj_AVAudioSource *self, void *closure ) {
    Py_INCREF(pyAudioSourceFuncs);
    return pyAudioSourceFuncs;
}

static PyGetSetDef AVAudioSource_getsetters[] = {
    { AUDIO_FRAME_SOURCE_FUNCS, (getter) AVAudioSource_getFuncs, NULL, "Audio frame source C API." },
    { NULL }
};

static PyTypeObject py_type_AVAudioSource = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.libav.AVAudioSource",
    .tp_basicsize = sizeof(py_obj_AVAudioSource),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_AudioSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AVAudioSource_dealloc,
    .tp_init = (initproc) AVAudioSource_init,
    .tp_getset = AVAudioSource_getsetters,
//    .tp_methods = AVAudioSource_methods
};

void init_AVAudioSource( PyObject *module ) {
    if( PyType_Ready( &py_type_AVAudioSource ) < 0 )
        return;

    Py_INCREF( &py_type_AVAudioSource );
    PyModule_AddObject( module, "AVAudioSource", (PyObject *) &py_type_AVAudioSource );

    pyAudioSourceFuncs = PyCapsule_New( &audioSourceFuncs,
        AUDIO_FRAME_SOURCE_FUNCS, NULL );
}

