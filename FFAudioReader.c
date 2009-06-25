
#include "framework.h"
#include <libavformat/avformat.h>

/******** AVAudioReader *********/
typedef struct {
    PyObject_HEAD

    AVFormatContext *context;
    AVCodecContext *codecContext;
    AVCodec *codec;
    int firstAudioStream;
    AVFrame *inputFrame;
    bool allKeyframes;
    int16_t *scratchyRadioBuffer;
    int lastPacketStart, lastPacketDuration;
} py_obj_AVAudioReader;

static int
AVAudioReader_init( py_obj_AVAudioReader *self, PyObject *args, PyObject *kwds ) {
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

    self->scratchyRadioBuffer = malloc( sizeof(int16_t) * AVCODEC_MAX_AUDIO_FRAME_SIZE );

    if( !self->scratchyRadioBuffer ) {
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
AVAudioReader_dealloc( py_obj_AVAudioReader *self ) {
    if( self->scratchyRadioBuffer != NULL ) {
        free( self->scratchyRadioBuffer );
        self->scratchyRadioBuffer = NULL;
    }

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

static void
AVAudioReader_getFrame( py_obj_AVAudioReader *self, AudioFrame *frame ) {
/*    if( frameIndex < 0 || frameIndex > self->context->streams[self->firstAudioStream]->duration ) {
        // No result
        frame->currentMaxSample = -1;
        frame->currentMinSample = 0;
        return;
    }*/

    //printf( "Requested %ld\n", frameIndex );

    int64_t timestamp = frame->fullMinSample;

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
    int channelCount = min(frame->channelCount, self->codecContext->channels);

    if( self->lastPacketStart != -1 && self->lastPacketStart <= frame->fullMaxSample &&
        (self->lastPacketStart + self->lastPacketDuration) >= frame->fullMinSample ) {

        // Decode into the current frame
        for( int sample = max(self->lastPacketStart, frame->fullMinSample); sample < min(self->lastPacketStart + self->lastPacketDuration, frame->fullMaxSample + 1); sample++ ) {
            for( int channel = 0; channel < channelCount; channel++ ) {
                frame->frameData[frame->channelCount * (sample - frame->fullMinSample) + channel] =
                    (float) self->scratchyRadioBuffer[(sample - self->lastPacketStart) * self->codecContext->channels + channel] * (1.0f / 32768.0f);
            }
        }

        frame->currentMinSample = self->lastPacketStart;
        frame->currentMaxSample = self->lastPacketDuration;
        first = false;
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

        int dataSize = sizeof(int16_t) * AVCODEC_MAX_AUDIO_FRAME_SIZE;

        //printf( "Decoding video\n" );
        // BJC: Here. Enjoy this marvelous cosmic joke: FFMPEG decodes all kinds of formats
        // with exquisite attention to detail. Then it hammers them all into 16-bit shorts.
        if( avcodec_decode_audio2( self->codecContext, self->scratchyRadioBuffer, &dataSize,
            packet.data, packet.size ) < 0 ) {
            printf( "Could not decode the audio.\n" );
            frame->currentMaxSample = -1;
            frame->currentMinSample = 0;
            return;
        }

        if( dataSize <= 0 ) {
            //printf( "Didn't get a picture\n" );
            av_free_packet( &packet );
            continue;
        }

        self->lastPacketStart = packet.dts;
        self->lastPacketDuration = packet.duration;

        //printf( "We'll take that\n" );
        for( int sample = max(packet.dts, frame->fullMinSample); sample < min(packet.dts + packet.duration, frame->fullMaxSample + 1); sample++ ) {
            for( int channel = 0; channel < channelCount; channel++ ) {
                frame->frameData[frame->channelCount * (sample - frame->fullMinSample) + channel] =
                    (float) self->scratchyRadioBuffer[(sample - packet.dts) * self->codecContext->channels + channel] * (1.0f / 32768.0f);
            }
        }

        if( first ) {
            frame->currentMinSample = packet.dts;
            frame->currentMaxSample = packet.dts + packet.duration - 1;
            first = false;
        }
        else {
            frame->currentMinSample = min(frame->currentMinSample, packet.dts);
            frame->currentMaxSample = max(frame->currentMaxSample, packet.dts + packet.duration - 1);
        }

        frame->currentMinSample = max(frame->currentMinSample, frame->fullMinSample);
        frame->currentMaxSample = min(frame->currentMaxSample, frame->fullMaxSample);

        av_free_packet( &packet );

        if( packet.dts + packet.duration >= frame->fullMaxSample ) {
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
    (audio_getFrameFunc) AVAudioReader_getFrame
};

static PyObject *pyAudioSourceFuncs;

static PyObject *
AVAudioReader_getFuncs( py_obj_AVAudioReader *self, void *closure ) {
    return pyAudioSourceFuncs;
}

static PyGetSetDef AVAudioReader_getsetters[] = {
    { "_audioFrameSourceFuncs", (getter) AVAudioReader_getFuncs, NULL, "Audio frame source C API." },
    { NULL }
};

static PyTypeObject py_type_AVAudioReader = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.AVAudioReader",    // tp_name
    sizeof(py_obj_AVAudioReader),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AVAudioReader_dealloc,
    .tp_init = (initproc) AVAudioReader_init,
    .tp_getset = AVAudioReader_getsetters,
//    .tp_methods = AVAudioReader_methods
};

NOEXPORT void init_FFAudioReader( PyObject *module ) {
    if( PyType_Ready( &py_type_AVAudioReader ) < 0 )
        return;

    Py_INCREF( &py_type_AVAudioReader );
    PyModule_AddObject( module, "AVAudioReader", (PyObject *) &py_type_AVAudioReader );

    pyAudioSourceFuncs = PyCObject_FromVoidPtr( &audioSourceFuncs, NULL );
}

