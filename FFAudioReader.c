
#include "framework.h"
#include <libavformat/avformat.h>

/******** FFAudioReader *********/
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
} py_obj_FFAudioReader;

static int
FFAudioReader_init( py_obj_FFAudioReader *self, PyObject *args, PyObject *kwds ) {
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

    if( (error = av_open_input_file( &self->context, filename, NULL, 0, NULL )) != 0 ) {
        PyErr_Format( PyExc_Exception, "Could not open the file (%s).", strerror( -error ) );
        return -1;
    }

    if( (error = av_find_stream_info( self->context )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Could not find the stream info (%s).", strerror( -error ) );
        return -1;
    }

    for( int i = 0; i < self->context->nb_streams; i++ ) {
        if( self->context->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO ) {
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

    if( (error = avcodec_open( self->codecContext, self->codec )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Could not open a codec (%s).", strerror( -error ) );
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
FFAudioReader_dealloc( py_obj_FFAudioReader *self ) {
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

    self->ob_type->tp_free( (PyObject*) self );
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

static void convert_samples( float *out, int outChannels, void *in, int inChannels, int offset, enum SampleFormat sample_fmt, int duration ) {
    switch( sample_fmt ) {
        case SAMPLE_FMT_U8:
            convert_uint8( out, outChannels,
                ((uint8_t *) in) + offset * inChannels, inChannels, duration );
            return;

        case SAMPLE_FMT_S16:
            convert_int16( out, outChannels,
                ((int16_t *) in) + offset * inChannels, inChannels, duration );
            return;

        case SAMPLE_FMT_S32:
            convert_int32( out, outChannels,
                ((int32_t *) in) + offset * inChannels, inChannels, duration );
            return;

        case SAMPLE_FMT_FLT:
            convert_float( out, outChannels,
                ((float *) in) + offset * inChannels, inChannels, duration );
            return;

        case SAMPLE_FMT_DBL:
            convert_double( out, outChannels,
                ((double *) in) + offset * inChannels, inChannels, duration );
            return;

        default:
            printf( "Unknown sample type.\n" );
            return;
    }
}

static int getSampleCount( int byteCount, enum SampleFormat sample_fmt, int channels ) {
    static int formatSize[] = { 1, 2, 4, 4, 8 };

    if( sample_fmt < 0 || sample_fmt > 4 )
        sample_fmt = 1;

    return byteCount / (formatSize[sample_fmt] * channels);
}

static void
FFAudioReader_getFrame( py_obj_FFAudioReader *self, AudioFrame *frame ) {
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
    int64_t timestamp = ((int64_t) frame->fullMinSample * timeBase->den) / (timeBase->num * sampleRate) + frameDuration / 2;


//    if( (uint64_t) self->context->start_time != AV_NOPTS_VALUE )
//        timestamp += self->context->start_time;

/*    if( self->allKeyframes ) {
        if( av_seek_frame( self->context, self->firstAudioStream, frame->fullMinSample,
                AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD ) < 0 )
            printf( "Could not seek to frame %ld.\n", frame->fullMinSample );

        self->currentAudioFrame = frame->fullMinSample;
    }
    else {*/
        // Only bother seeking if we're way off (or it's behind us)
        if( self->lastPacketStart != -1 && (frame->fullMinSample < self->lastPacketStart || (frame->fullMinSample - self->lastPacketStart) >= (frame->fullMaxSample - frame->fullMinSample) * 4) ) {

            //printf( "min: %d, lastPacket: %d\n", frame->fullMinSample, self->lastPacketStart );
            //printf( "Seeking back to %ld...\n", timestamp );
            int seekStamp = timestamp;

            if( seekStamp < 0 )
                seekStamp = 0;

            av_seek_frame( self->context, self->firstAudioStream, seekStamp, AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD );
        }
//    }

    bool first = true;

    if( self->lastPacketStart != -1 && self->lastPacketStart <= frame->fullMaxSample &&
        (self->lastPacketStart + self->lastPacketDuration) >= frame->fullMinSample ) {

        // Decode into the current frame
        int startSample = max(self->lastPacketStart, frame->fullMinSample);
        int duration = min(self->lastPacketStart + self->lastPacketDuration, frame->fullMaxSample + 1) - startSample;
        float *out = frame->frameData + (startSample - frame->fullMinSample) * frame->channelCount;

        convert_samples( out, frame->channelCount, self->audioBuffer, self->codecContext->channels, (startSample - self->lastPacketStart),
            self->codecContext->sample_fmt, duration );

        frame->currentMinSample = startSample;
        frame->currentMaxSample = startSample + duration - 1;
        first = false;

        // We could be done...
        if( frame->currentMinSample == frame->fullMinSample && frame->currentMaxSample == frame->fullMaxSample ) {
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
        uint8_t *data = packet.data;
        int dataSize = packet.size;
        void *audioBuffer = self->audioBuffer;

        int packetStart = (packet.dts * timeBase->num * sampleRate) / (timeBase->den);
        int packetDuration = 0;

        while( dataSize > 0 ) {
            int decoded;

            //printf( "Decoding audio (size left: %d)\n", dataSize );
            if( (decoded = avcodec_decode_audio2( self->codecContext, audioBuffer, &bufferSize,
                data, dataSize )) < 0 ) {
                printf( "Could not decode the audio.\n" );
                frame->currentMaxSample = -1;
                frame->currentMinSample = 0;
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

            data += decoded;
            dataSize -= decoded;
            audioBuffer += bufferSize;
            bufferSize = AVCODEC_MAX_AUDIO_FRAME_SIZE;        // A lie, but a safe one, I think
        }

        self->lastPacketStart = packetStart;
        self->lastPacketDuration = packetDuration;

        //printf( "We'll take that (%d, %d)\n", packetStart, packetDuration );
        int startSample = max(packetStart, frame->fullMinSample);
        int duration = min(packetStart + packetDuration, frame->fullMaxSample + 1) - startSample;
        float *out = frame->frameData + (startSample - frame->fullMinSample) * frame->channelCount;

        convert_samples( out, frame->channelCount, self->audioBuffer, self->codecContext->channels, (startSample - packetStart),
            self->codecContext->sample_fmt, duration );

        if( first ) {
            frame->currentMinSample = packetStart;
            frame->currentMaxSample = packetStart + packetDuration - 1;
            first = false;
        }
        else {
            frame->currentMinSample = min(frame->currentMinSample, packetStart);
            frame->currentMaxSample = max(frame->currentMaxSample, packetStart + packetDuration - 1);
        }

        frame->currentMinSample = max(frame->currentMinSample, frame->fullMinSample);
        frame->currentMaxSample = min(frame->currentMaxSample, frame->fullMaxSample);

        av_free_packet( &packet );

        if( packetStart + packetDuration >= frame->fullMaxSample ) {
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
    (audio_getFrameFunc) FFAudioReader_getFrame
};

static PyObject *pyAudioSourceFuncs;

static PyObject *
FFAudioReader_getFuncs( py_obj_FFAudioReader *self, void *closure ) {
    return pyAudioSourceFuncs;
}

static PyGetSetDef FFAudioReader_getsetters[] = {
    { "_audioFrameSourceFuncs", (getter) FFAudioReader_getFuncs, NULL, "Audio frame source C API." },
    { NULL }
};

static PyTypeObject py_type_FFAudioReader = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.FFAudioReader",    // tp_name
    sizeof(py_obj_FFAudioReader),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) FFAudioReader_dealloc,
    .tp_init = (initproc) FFAudioReader_init,
    .tp_getset = FFAudioReader_getsetters,
//    .tp_methods = FFAudioReader_methods
};

NOEXPORT void init_FFAudioReader( PyObject *module ) {
    if( PyType_Ready( &py_type_FFAudioReader ) < 0 )
        return;

    Py_INCREF( &py_type_FFAudioReader );
    PyModule_AddObject( module, "FFAudioReader", (PyObject *) &py_type_FFAudioReader );

    pyAudioSourceFuncs = PyCObject_FromVoidPtr( &audioSourceFuncs, NULL );
}

