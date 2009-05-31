
#include "framework.h"
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

typedef struct {
    PyObject_HEAD

    AVFormatContext *context;
    AVCodecContext *codecContext;
    AVCodec *codec;
    int firstVideoStream;
    AVFrame *inputFrame, *rgbFrame;
    uint8_t *rgbBuffer;
    float colorMatrix[3][3];
    struct SwsContext *scaler;
    bool allKeyframes;
    int currentVideoFrame;
} py_obj_AVVideoReader;

/*static float gamma22ExpandFunc( float input ) {
    return powf( input, 2.2f );
}*/

static half gamma22[65536];

/*static halfFunction<half> __gamma22( gamma22ExpandFunc, half( -2.0f ), half( 2.0f ) );*/

static int
AVVideoReader_init( py_obj_AVVideoReader *self, PyObject *args, PyObject *kwds ) {
    int error;
    char *filename;

    // Zero all pointers (so we know later what needs deleting)
    self->context = NULL;
    self->codecContext = NULL;
    self->codec = NULL;
    self->inputFrame = NULL;
    self->rgbFrame = NULL;
    self->rgbBuffer = NULL;
    self->scaler = NULL;

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
        if( self->context->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO ) {
            self->firstVideoStream = i;
            break;
        }
    }

    if( self->firstVideoStream == -1 ) {
        PyErr_SetString( PyExc_Exception, "Could not find a video stream." );
        return -1;
    }

    self->codecContext = self->context->streams[self->firstVideoStream]->codec;
    self->codec = avcodec_find_decoder( self->codecContext->codec_id );

    if( self->codec == NULL ) {
        PyErr_SetString( PyExc_Exception, "Could not find a codec for the stream." );
        return -1;
    }

    if( (error = avcodec_open( self->codecContext, self->codec )) < 0 ) {
        PyErr_Format( PyExc_Exception, "Could not open a codec (%s).", strerror( -error ) );
        return -1;
    }

    if( (self->inputFrame = avcodec_alloc_frame()) == NULL ) {
        PyErr_Format( PyExc_Exception, "Could not allocate input frame (%s).", strerror( ENOMEM ) );
        return -1;
    }

    if( (self->rgbFrame = avcodec_alloc_frame()) == NULL ) {
        PyErr_Format( PyExc_Exception, "Could not allocate output frame (%s).", strerror( ENOMEM ) );
        return -1;
    }

    int byteCount = avpicture_get_size( PIX_FMT_YUV444P, self->codecContext->width,
        self->codecContext->height );

    if( (self->rgbBuffer = (uint8_t*) av_malloc( byteCount )) == NULL ) {
        PyErr_Format( PyExc_Exception, "Could not allocate output frame buffer (%s).", strerror( ENOMEM ) );
        return -1;
    }

    avpicture_fill( (AVPicture *) self->rgbFrame, self->rgbBuffer, PIX_FMT_YUV444P,
        self->codecContext->width, self->codecContext->height );

    // Rec. 601 weights courtesy of http://www.poynton.com/notes/colour_and_gamma/ColorFAQ.html
/*    self->colorMatrix[0][0] = 1.0f / 219.0f;    // Y -> R
    self->colorMatrix[0][1] = 0.0f;    // Pb -> R, and so on
    self->colorMatrix[0][2] = 1.402f / 244.0f;
    self->colorMatrix[1][0] = 1.0f / 219.0f;
    self->colorMatrix[1][1] = -0.344136f / 244.0f;
    self->colorMatrix[1][2] = -0.714136f / 244.0f;
    self->colorMatrix[2][0] = 1.0f / 219.0f;
    self->colorMatrix[2][1] = 1.772f / 244.0f;
    self->colorMatrix[2][2] = 0.0f;*/

    // Naturally, this page disappeared soon after I referenced it, these are from intersil AN9717
    self->colorMatrix[0][0] = 1.0f;
    self->colorMatrix[0][1] = 0.0f;
    self->colorMatrix[0][2] = 1.371f;
    self->colorMatrix[1][0] = 1.0f;
    self->colorMatrix[1][1] = -0.336f;
    self->colorMatrix[1][2] = -0.698f;
    self->colorMatrix[2][0] = 1.0f;
    self->colorMatrix[2][1] = 1.732f;
    self->colorMatrix[2][2] = 0.0f;

    self->scaler = sws_getContext(
        self->codecContext->width, self->codecContext->height, self->codecContext->pix_fmt,
        self->codecContext->width, self->codecContext->height, PIX_FMT_YUV444P, SWS_POINT,
        NULL, NULL, NULL );

    if( self->scaler == NULL ) {
        PyErr_Format( PyExc_Exception, "Could not allocate scaler (%s).", strerror( ENOMEM ) );
        return -1;
    }

    self->currentVideoFrame = 0;

    // Use MLT's keyframe conditions
    self->allKeyframes = !(strcmp( self->codecContext->codec->name, "mjpeg" ) &&
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
    }

    return 0;
}

static void
AVVideoReader_dealloc( py_obj_AVVideoReader *self ) {
    if( self->scaler != NULL ) {
        sws_freeContext( self->scaler );
        self->scaler = NULL;
    }

    if( self->rgbBuffer != NULL ) {
        av_free( self->rgbBuffer );
        self->rgbBuffer = NULL;
    }

    if( self->rgbFrame != NULL ) {
        av_free( self->rgbFrame );
        self->rgbFrame = NULL;
    }

    if( self->inputFrame != NULL ) {
        av_free( self->inputFrame );
        self->inputFrame = NULL;
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
AVVideoReader_getFrame( py_obj_AVVideoReader *self, int64_t frameIndex, RgbaFrame *frame ) {
    if( frameIndex < 0 || frameIndex > self->context->streams[self->firstVideoStream]->duration ) {
        // No result
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

    //printf( "Requested %ld\n", frameIndex );

    AVRational *timeBase = &self->context->streams[self->firstVideoStream]->time_base;
    AVRational *frameRate = &self->context->streams[self->firstVideoStream]->r_frame_rate;
    int64_t frameDuration = (timeBase->den * frameRate->den) / (timeBase->num * frameRate->num);
    int64_t timestamp = frameIndex * (timeBase->den * frameRate->den) / (timeBase->num * frameRate->num) + frameDuration / 2;
    //printf( "frameRate: %d/%d\n", frameRate->num, frameRate->den );
    //printf( "frameDuration: %ld\n", frameDuration );

//    if( (uint64_t) self->context->start_time != AV_NOPTS_VALUE )
//        timestamp += self->context->start_time;

    if( self->allKeyframes ) {
        if( av_seek_frame( self->context, self->firstVideoStream, frameIndex,
                AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD ) < 0 )
            printf( "Could not seek to frame %ld.\n", frameIndex );

        self->currentVideoFrame = frameIndex;
    }
    else {
        // Only bother seeking if we're way off (or it's behind us)
        if( frameIndex < self->currentVideoFrame || (frameIndex - self->currentVideoFrame) >= 15 ) {

            //printf( "Seeking back to %ld...\n", timestamp );
            int seekStamp = timestamp - frameDuration * 3;

            if( seekStamp < 0 )
                seekStamp = 0;

            av_seek_frame( self->context, self->firstVideoStream, seekStamp, AVSEEK_FLAG_BACKWARD );
        }

        self->currentVideoFrame = frameIndex;
    }

    for( ;; ) {
        AVPacket packet;

        av_init_packet( &packet );

        //printf( "Reading frame\n" );
        if( av_read_frame( self->context, &packet ) < 0 )
            printf( "Could not read the frame.\n" );

        if( packet.stream_index != self->firstVideoStream ) {
            //printf( "Not the right stream\n" );
            av_free_packet( &packet );
            continue;
        }

        int gotPicture;

        //printf( "Decoding video\n" );
        avcodec_decode_video( self->codecContext, self->inputFrame, &gotPicture,
            packet.data, packet.size );

        if( !gotPicture ) {
            //printf( "Didn't get a picture\n" );
            av_free_packet( &packet );
            continue;
        }

        if( (packet.dts + frameDuration) < timestamp ) {
            //printf( "Too early (%ld vs %ld)\n", packet.dts, timestamp );
            av_free_packet( &packet );
            continue;
        }

        //printf( "We'll take that\n" );

        AVFrame *avFrame = self->rgbFrame;

#if DO_SCALE
        if( sws_scale( self->scaler, self->inputFrame->data, self->inputFrame->linesize, 0,
                self->codecContext->height, self->rgbFrame->data, self->rgbFrame->linesize ) < self->codecContext->height ) {
            av_free_packet( &packet );
            THROW( Iex::BaseExc, "The image conversion failed." );
        }
#else
        avFrame = self->inputFrame;
#endif

        // Now convert to halfs
        frame->currentDataWindow.min.x = max( 0, frame->currentDataWindow.min.x );
        frame->currentDataWindow.min.y = max( 0, frame->currentDataWindow.min.y );
        frame->currentDataWindow.max.x = min( self->codecContext->width - 1, frame->currentDataWindow.max.x );
        frame->currentDataWindow.max.y = min( self->codecContext->height - 1, frame->currentDataWindow.max.y );

        box2i coordWindow = {
            { frame->currentDataWindow.min.x - frame->fullDataWindow.min.x, frame->currentDataWindow.min.y - frame->fullDataWindow.min.y },
            { frame->currentDataWindow.max.x - frame->fullDataWindow.min.x, frame->currentDataWindow.max.y - frame->fullDataWindow.min.y }
        };

        //printf( "pix_fmt: %d\n", self->codecContext->pix_fmt );

        if( self->codecContext->pix_fmt == PIX_FMT_YUV411P ) {
            uint8_t *yplane = avFrame->data[0], *cbplane = avFrame->data[1], *crplane = avFrame->data[2];
            half a = 1.0f;
            const float __unbyte = 1.0f / 255.0f;

            for( int row = coordWindow.min.y; row <= coordWindow.max.y; row++ ) {
    #if DO_SCALE
                for( int x = 0; x < self->codecContext->width; x++ ) {
                    float y = yplane[x] - 16.0f, cb = cbplane[x] - 128.0f, cr = crplane[x] - 128.0f;

                    frame->base[row * frame->stride + x].r = __gamma22( y * self->colorMatrix[0][0] + cb * self->colorMatrix[0][1] +
                        cr * self->colorMatrix[0][2] );
                    frame->base[row * frame->stride + x].g = __gamma22( y * self->colorMatrix[1][0] + cb * self->colorMatrix[1][1] +
                        cr * self->colorMatrix[1][2] );
                    frame->base[row * frame->stride + x].b = __gamma22( y * self->colorMatrix[2][0] + cb * self->colorMatrix[2][1] +
                        cr * self->colorMatrix[2][2] );
                    frame->base[row * frame->stride + x].a = a;
                }
    #else
                for( int x = coordWindow.min.x / 4; x <= coordWindow.max.x / 4; x++ ) {
                    float cb = cbplane[x] - 128.0f, cr = crplane[x] - 128.0f;

                    float ccr = cb * self->colorMatrix[0][1] + cr * self->colorMatrix[0][2];
                    float ccg = cb * self->colorMatrix[1][1] + cr * self->colorMatrix[1][2];
                    float ccb = cb * self->colorMatrix[2][1] + cr * self->colorMatrix[2][2];

                    for( int i = 0; i < 4; i++ ) {
                        int px = x * 4 + i;

                        if( px < coordWindow.min.x || px > coordWindow.max.x )
                            continue;

                        float y = yplane[x * 4 + i] - 16.0f;

                        frame->frameData[row * frame->stride + x * 4 + i].r =
                            gamma22[f2h( (y * self->colorMatrix[0][0] + ccr) * __unbyte )];
                        frame->frameData[row * frame->stride + x * 4 + i].g =
                            gamma22[f2h( (y * self->colorMatrix[1][0] + ccg) * __unbyte )];
                        frame->frameData[row * frame->stride + x * 4 + i].b =
                            gamma22[f2h( (y * self->colorMatrix[2][0] + ccb) * __unbyte )];
                        frame->frameData[row * frame->stride + x * 4 + i].a = a;
                    }
                }
    #endif

                yplane += avFrame->linesize[0];
                cbplane += avFrame->linesize[1];
                crplane += avFrame->linesize[2];
            }
        }
        else if( self->codecContext->pix_fmt == PIX_FMT_YUV420P ) {
            uint8_t *restrict yplane = avFrame->data[0], *restrict cbplane = avFrame->data[1], *restrict crplane = avFrame->data[2];
            rgba *restrict frameData = frame->frameData;

            half a = f2h( 1.0f );
            int pyi = avFrame->interlaced_frame ? 2 : 1;

            // 4:2:0 interlaced:
            // 0 -> 0, 2; 2 -> 4, 6; 4 -> 8, 10
            // 1 -> 1, 3; 3 -> 5, 7; 5 -> 9, 11

            // 4:2:0 progressive:
            // 0 -> 0, 1; 1 -> 2, 3; 2 -> 4, 5

            const int minsx = coordWindow.min.x >> 1, maxsx = coordWindow.max.x >> 1;

            for( int sy = coordWindow.min.y / 2; sy <= coordWindow.max.y / 2; sy++ ) {
                int py = sy * 2;

                if( avFrame->interlaced_frame && (sy & 1) == 1 )
                    py--;

                for( int i = 0; i < 2; i++ ) {
                    uint8_t *restrict cbx = cbplane + minsx, *restrict crx = crplane + minsx;

                    for( int sx = minsx; sx <= maxsx; sx++ ) {
                        float cb = *cbx++ - 128.0f, cr = *crx++ - 128.0f;

                        float ccr = cb * self->colorMatrix[0][1] + cr * self->colorMatrix[0][2];
                        float ccg = cb * self->colorMatrix[1][1] + cr * self->colorMatrix[1][2];
                        float ccb = cb * self->colorMatrix[2][1] + cr * self->colorMatrix[2][2];

                        if( py < coordWindow.min.y || py > coordWindow.max.y )
                            continue;

                        for( int px = sx * 2; px <= sx * 2 + 1; px++ ) {
                            if( px < coordWindow.min.x || px > coordWindow.max.x )
                                continue;

                            float cy = yplane[avFrame->linesize[0] * (py + pyi * i) + px] - 16.0f;

                            frameData[(py + pyi * i) * frame->stride + px].r =
                                gamma22[f2h((cy * self->colorMatrix[0][0] + ccr))];
                            frameData[(py + pyi * i) * frame->stride + px].g =
                                gamma22[f2h((cy * self->colorMatrix[1][0] + ccg))];
                            frameData[(py + pyi * i) * frame->stride + px].b =
                                gamma22[f2h((cy * self->colorMatrix[2][0] + ccb))];
                            frameData[(py + pyi * i) * frame->stride + px].a = a;
                        }
                    }
                }

                cbplane += avFrame->linesize[1];
                crplane += avFrame->linesize[2];
            }
        }
        else {
            box2i_setEmpty( &frame->currentDataWindow );
        }

        av_free_packet( &packet );
        return;
    }
}

static VideoFrameSourceFuncs videoSourceFuncs = {
    0,
    (video_getFrameFunc) AVVideoReader_getFrame
};

static PyObject *pyVideoSourceFuncs;

static PyObject *
AVVideoReader_getFuncs( py_obj_AVVideoReader *self, void *closure ) {
    return pyVideoSourceFuncs;
}

static PyGetSetDef AVVideoReader_getsetters[] = {
    { "_videoFrameSourceFuncs", (getter) AVVideoReader_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PyObject *
AVVideoReader_size( py_obj_AVVideoReader *self ) {
    return Py_BuildValue( "(ii)", self->codecContext->width, self->codecContext->height );
}

static PyMethodDef AVVideoReader_methods[] = {
    { "size", (PyCFunction) AVVideoReader_size, METH_NOARGS,
        "Gets the frame size for this video." },
    { NULL }
};

static PyTypeObject py_type_AVVideoReader = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.AVVideoReader",    // tp_name
    sizeof(py_obj_AVVideoReader),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AVVideoReader_dealloc,
    .tp_init = (initproc) AVVideoReader_init,
    .tp_getset = AVVideoReader_getsetters,
    .tp_methods = AVVideoReader_methods
};


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

    self->lastPacketStart = -1;

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

    AVRational *timeBase = &self->context->streams[self->firstAudioStream]->time_base;
    printf( "timebase: %d/%d\n", timeBase->num, timeBase->den );

//    int64_t frameDuration = (timeBase->den * frameRate->den) / (timeBase->num * frameRate->num);
    int64_t timestamp = frame->fullMinSample;
    //printf( "frameRate: %d/%d\n", frameRate->num, frameRate->den );
    //printf( "frameDuration: %ld\n", frameDuration );

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
        if( self->lastPacketStart != -1 && (frame->fullMinSample < self->lastPacketStart || (frame->fullMinSample - self->lastPacketStart) >= 48000) ) {

            printf( "Seeking back to %ld...\n", timestamp );
            int seekStamp = timestamp - 24000;

            if( seekStamp < 0 )
                seekStamp = 0;

            av_seek_frame( self->context, self->firstAudioStream, seekStamp, AVSEEK_FLAG_BACKWARD );
        }
//    }

    bool first = true;
    int channelCount = min(frame->channelCount, self->codecContext->channels);

    for( ;; ) {
        if( first && self->lastPacketStart != -1 && self->lastPacketStart <= frame->fullMaxSample &&
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
            printf( "Too early (%ld + %d vs %ld)\n", packet.dts, packet.duration, timestamp );
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

        if( frame->currentMaxSample == frame->fullMaxSample ) {
            printf( "Enough: (%d, %d) vs (%d, %d)\n", frame->currentMinSample, frame->currentMaxSample, frame->fullMinSample, frame->fullMaxSample );
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






NOEXPORT void init_AVFileReader( PyObject *module ) {
    const float __unbyte = 1.0f / 255.0f;

    for( int i = 0; i < 65536; i++ ) {
        gamma22[i] = f2h( powf( h2f( (half) i ) * __unbyte, 2.2f ) );
    }

    if( PyType_Ready( &py_type_AVVideoReader ) < 0 )
        return;

    Py_INCREF( &py_type_AVVideoReader );
    PyModule_AddObject( module, "AVVideoReader", (PyObject *) &py_type_AVVideoReader );

    pyVideoSourceFuncs = PyCObject_FromVoidPtr( &videoSourceFuncs, NULL );

    if( PyType_Ready( &py_type_AVAudioReader ) < 0 )
        return;

    Py_INCREF( &py_type_AVAudioReader );
    PyModule_AddObject( module, "AVAudioReader", (PyObject *) &py_type_AVAudioReader );

    pyAudioSourceFuncs = PyCObject_FromVoidPtr( &audioSourceFuncs, NULL );
}



