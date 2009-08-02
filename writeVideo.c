
#include "framework.h"
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/avstring.h>

#define RAMP_SIZE    (1 << 16)

// studio YUV
// RGB -> YUV matrix
// chromaticities

PyObject *
py_writeVideo( PyObject *self, PyObject *args, PyObject *kw ) {
    rational videoRate = { 30000, 1001 };
    rational audioRate = { 48000, 1 };
    box2i dataWindow = { { 0, 0 }, { 719, 479 } };
    PyObject *videoSourceObj = NULL, *audioSourceObj = NULL;
    int64_t startTime = INT64_C(0), endTime = 5 * INT64_C(1000000000);
    char *filename = NULL;
    int audioChannels = 2;
    int result;

    static char *kwlist[] = { "filename", "videoSource", "audioSource", "startTime", "endTime", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "s|OOII", kwlist,
            &filename, &videoSourceObj, &audioSourceObj, &startTime, &endTime ) )
        return NULL;

    v2i frameSize;
    box2i_getSize( &dataWindow, &frameSize );

    VideoSourceHolder videoSource = { NULL };
    if( !takeVideoSource( videoSourceObj, &videoSource ) )
        return NULL;

    AudioSourceHolder audioSource = { NULL };
    if( !takeAudioSource( audioSourceObj, &audioSource ) )
        return NULL;

    // Do a simple gamma ramp for now
    uint8_t *ramp = slice_alloc( RAMP_SIZE );
    float *tempRampF = slice_alloc( RAMP_SIZE * sizeof(float) );
    half *tempRampH = slice_alloc( RAMP_SIZE * sizeof(half) );

    if( !ramp )
        return PyErr_NoMemory();

    for( int i = 0; i < RAMP_SIZE; i++ )
        tempRampH[i] = (half) i;

    half_convert_to_float( tempRampH, tempRampF, RAMP_SIZE );
    slice_free( RAMP_SIZE * sizeof(half), tempRampH );

    for( int i = 0; i < RAMP_SIZE; i++ ) {
        ramp[i] = (uint8_t) min( 255, max( 0,
            (int)(powf( min_f32( 1.0f, max_f32( 0.0f, tempRampF[i] ) ), 0.45f ) * 255.0f) ) );
    }

    slice_free( RAMP_SIZE * sizeof(float), tempRampF );

    // Does anyone know a better formula for a bit bucket size than this?
    // TODO: Make sure this is big enough for audio, too
    int bitBucketSize = (frameSize.x * frameSize.y) * 6 + 200;
    void *bitBucket = malloc( bitBucketSize );

    if( !bitBucket ) {
        return PyErr_NoMemory();
    }

    // Set up file
    AVOutputFormat *format = guess_format( "dv", /*filename*/ NULL, NULL );        // Static ptr

    if( format == NULL ) {
        PyErr_Format( PyExc_Exception, "Failed to find an output format matching %s.", filename );
        return NULL;
    }

    ByteIOContext *stream;

    if( url_fopen( &stream, filename, URL_WRONLY ) < 0 ) {
        PyErr_SetString( PyExc_Exception, "Failed to open the file." );
        return NULL;
    }

    AVFormatContext *context = avformat_alloc_context();
    context->oformat = format;
    context->pb = stream;
    av_strlcpy( context->filename, filename, sizeof(context->filename) );

    AVFormatParameters formatParams = { { 0 } };
    if( av_set_parameters( context, &formatParams ) < 0 ) {
        PyErr_SetString( PyExc_Exception, "Failed to set the format parameters." );
        return NULL;
    }

    AVStream *video = NULL, *audio = NULL;

    if( videoSource.funcs ) {
        video = av_new_stream( context, context->nb_streams );

        if( !video )
            return PyErr_NoMemory();

        // Be careful of the naming here: video->codec is the codec context
        avcodec_get_context_defaults2( video->codec, CODEC_TYPE_VIDEO );

        AVCodec *videoCodec = avcodec_find_encoder_by_name( "dvvideo" );
        video->codec->codec_id = videoCodec->id;

        video->codec->flags |= CODEC_FLAG_INTERLACED_DCT;

        if( format->flags & AVFMT_GLOBALHEADER ) {
            // The format wants the header, so don't put it in the stream
            video->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
        }

        //if (codec && codec->supported_framerates && !force_fps)
        //    fps = codec->supported_framerates[av_find_nearest_q_idx(fps, codec->supported_framerates)];
        video->codec->time_base.den = videoRate.n;
        video->codec->time_base.num = videoRate.d;

        video->codec->width = frameSize.x;
        video->codec->height = frameSize.y;
        video->codec->sample_aspect_ratio.num = 40;
        video->codec->sample_aspect_ratio.den = 33;
        video->codec->pix_fmt = PIX_FMT_YUV411P;
        video->sample_aspect_ratio = video->codec->sample_aspect_ratio;

        avcodec_open( video->codec, videoCodec );
    }

    if( audioSource.funcs ) {
        audio = av_new_stream( context, context->nb_streams );

        if( !audio )
            return PyErr_NoMemory();

        avcodec_get_context_defaults2( audio->codec, CODEC_TYPE_AUDIO );

        AVCodec *audioCodec = avcodec_find_encoder( CODEC_ID_PCM_S16LE );
        audio->codec->codec_id = audioCodec->id;

        if( format->flags & AVFMT_GLOBALHEADER ) {
            // The format wants the header, so don't put it in the stream
            audio->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
        }

        audio->codec->channels = audioChannels;
        audio->codec->sample_fmt = SAMPLE_FMT_S16;
        audio->codec->channel_layout = CH_LAYOUT_STEREO;
        audio->codec->sample_rate = audioRate.n / audioRate.d;
        audio->codec->time_base = (AVRational) { audioRate.d, audioRate.n };

    /*    if (audio_language) {
            av_metadata_set(&st->metadata, "language", audio_language);
            av_free(audio_language);
            audio_language = NULL;
        }*/

        avcodec_open( audio->codec, audioCodec );
    }

    /// context->preload = 0.5seconds (needed in some formats)

    struct SwsContext *scaler;

    scaler = sws_getContext(
        frameSize.x, frameSize.y, PIX_FMT_RGBA,
        frameSize.x, frameSize.y, video->codec->pix_fmt, SWS_FAST_BILINEAR,
        NULL, NULL, NULL );

    // Write header
    if( av_write_header( context ) < 0 ) {
        PyErr_SetString( PyExc_Exception, "Failed to write the header." );
        return NULL;
    }

    rgba_f16_frame inputFrame;
    AVFrame interFrame, outputFrame;
    uint8_t *interBuffer = NULL, *outputBuffer = NULL;
    int interBufferSize = 0, outputBufferSize = 0;

    if( videoSource.funcs ) {
        inputFrame.fullDataWindow = dataWindow;
        inputFrame.frameData = (rgba_f16*) slice_alloc( frameSize.x * frameSize.y * sizeof(rgba_f16) );
        inputFrame.stride = frameSize.x;

        avcodec_get_frame_defaults( &interFrame );
        avcodec_get_frame_defaults( &outputFrame );

        interBufferSize = avpicture_get_size(
            PIX_FMT_RGBA, frameSize.x, frameSize.y );

        outputBufferSize = avpicture_get_size(
            video->codec->pix_fmt, frameSize.x, frameSize.y );

        if( (interBuffer = (uint8_t*) slice_alloc( interBufferSize )) == NULL ) {
            return PyErr_NoMemory();
        }

        if( (outputBuffer = (uint8_t*) slice_alloc( outputBufferSize )) == NULL ) {
            return PyErr_NoMemory();
        }

        avpicture_fill( (AVPicture *) &interFrame, interBuffer,
            PIX_FMT_RGBA, frameSize.x, frameSize.y );
        avpicture_fill( (AVPicture *) &outputFrame, outputBuffer,
            video->codec->pix_fmt, frameSize.x, frameSize.y );
    }

    AudioFrame audioInputFrame;
    void *outSampleBuf = NULL;
    int outSampleBufSize = 0, sampleCount = 0;

    if( audioSource.funcs ) {
        if( audio->codec->frame_size > 1 )
            sampleCount = audio->codec->frame_size;
        else
            sampleCount = 1024;        // I have no idea

        outSampleBufSize = sizeof(short) * audioChannels * sampleCount;
        outSampleBuf = slice_alloc( outSampleBufSize );

        audioInputFrame.frameData = slice_alloc( sizeof(float) * audioChannels * sampleCount );
    }

    int nextVideoFrame = getTimeFrame( &videoRate, startTime );
    int nextAudioSample = getTimeFrame( &audioRate, startTime );
    int startVideoFrame = nextVideoFrame;

    int64_t time = startTime,
        nextVideoTime = getFrameTime( &videoRate, nextVideoFrame ),
        nextAudioTime = getFrameTime( &audioRate, nextAudioSample );

    while( time <= endTime ) {
        AVPacket packet;
        av_init_packet( &packet );

        if( time == nextVideoTime ) {
            //printf( "video #%d\n", nextVideoFrame );
            packet.stream_index = video->index;

            inputFrame.currentDataWindow = inputFrame.fullDataWindow;
            videoSource.funcs->getFrame( videoSource.source, nextVideoFrame, &inputFrame );

            // Transcode to RGBA
            for( int y = 0; y < frameSize.y; y++ ) {
                rgba_u8 *targetData = (rgba_u8*) &interFrame.data[0][y * interFrame.linesize[0]];
                rgba_f16 *sourceData = &inputFrame.frameData[y * inputFrame.stride];

                for( int x = 0; x < frameSize.x; x++ ) {
                    targetData[x].r = ramp[sourceData[x].r];
                    targetData[x].g = ramp[sourceData[x].g];
                    targetData[x].b = ramp[sourceData[x].b];
                    targetData[x].a = ramp[sourceData[x].a];
                }

//                printf( "RGBA: %d, %d, %d, %d", (int) targetData[0].r,
//                    (int) targetData[0].g, (int) targetData[0].b, (int) targetData[0].a );
            }

            // Use swscale to make it YUV 4:1:1
            sws_scale( scaler, interFrame.data, interFrame.linesize,
                0, frameSize.y, outputFrame.data, outputFrame.linesize );

            // Write to file
            outputFrame.interlaced_frame = 1;
            outputFrame.top_field_first = 0;
            outputFrame.pts = nextVideoFrame - startVideoFrame;

            result = avcodec_encode_video( video->codec,
                bitBucket, bitBucketSize, &outputFrame );

            if( result < 0 ) {
                PyErr_SetString( PyExc_Exception, "Video encoding failed." );
                return NULL;
            }

            if( result > 0 ) {
                packet.data = bitBucket;
                packet.size = result;

                if( video->codec->coded_frame->pts != AV_NOPTS_VALUE ) {
                    packet.pts = av_rescale_q( video->codec->coded_frame->pts,
                        video->codec->time_base, video->time_base );
                }

                if( video->codec->coded_frame->key_frame )
                    packet.flags |= PKT_FLAG_KEY;

                if( av_interleaved_write_frame( context, &packet ) < 0 ) {
                    PyErr_SetString( PyExc_Exception, "Failed to write frame." );
                    return NULL;
                }
            }

            nextVideoFrame++;
            nextVideoTime = getFrameTime( &videoRate, nextVideoFrame );
        }

        if( time == nextAudioTime && audioSource.funcs ) {
            //printf( "audio #%d\n", nextAudioSample );
            packet.stream_index = audio->index;

            audioInputFrame.channelCount = audioChannels;
            audioInputFrame.fullMinSample = nextAudioSample;
            audioInputFrame.fullMaxSample = nextAudioSample + sampleCount - 1;
            audioInputFrame.currentMinSample = nextAudioSample;
            audioInputFrame.currentMaxSample = nextAudioSample + sampleCount - 1;

            audioSource.funcs->getFrame( audioSource.source, &audioInputFrame );

            // TODO: Handle incomplete returned frame
            // TODO: Handle other sample types
            for( int i = 0; i < sampleCount; i++ ) {
                for( int j = 0; j < audioChannels; j++ ) {
                    ((short*) outSampleBuf)[i * audioChannels + j] =
                        (short)(audioInputFrame.frameData[i * audioChannels + j] * 32767.0);
                }
            }

            int bufSize;

            if( audio->codec->frame_size > 1 ) {
                // We can set the real buffer size
                bufSize = outSampleBufSize;
            }
            else {
                // Tell them how much we want in the buffer (based on size of in/out samples)
                bufSize = sampleCount * audioChannels * sizeof(short);
            }

            result = avcodec_encode_audio( audio->codec,
                bitBucket, bufSize, outSampleBuf );

            if( result < 0 ) {
                PyErr_SetString( PyExc_Exception, "Video encoding failed." );
                return NULL;
            }

            if( result > 0 ) {
                packet.data = bitBucket;
                packet.size = result;

                if( audio->codec->coded_frame->pts != AV_NOPTS_VALUE ) {
                    packet.pts = av_rescale_q( audio->codec->coded_frame->pts,
                        audio->codec->time_base, audio->time_base );
                }
                else {
                    packet.pts = av_rescale_q( audioInputFrame.fullMinSample,
                        audio->codec->time_base, audio->time_base );
                }

                if( audio->codec->coded_frame->key_frame )
                    packet.flags |= PKT_FLAG_KEY;

                if( av_interleaved_write_frame( context, &packet ) < 0 ) {
                    PyErr_SetString( PyExc_Exception, "Failed to write frame." );
                    return NULL;
                }
            }

            nextAudioSample += sampleCount;
            nextAudioTime = getFrameTime( &audioRate, nextAudioSample );
        }

        if( videoSource.funcs ) {
            if( audioSource.funcs )
                time = (nextAudioTime < nextVideoTime) ? nextAudioTime : nextVideoTime;
            else
                time = nextVideoTime;
        }
        else
            time = nextAudioTime;
    }

    // Flush video
    if( videoSource.funcs ) {
        result = avcodec_encode_video( video->codec, bitBucket, bitBucketSize, NULL );

        if( result < 0 ) {
            PyErr_SetString( PyExc_Exception, "Video encoding failed." );
            return NULL;
        }

        if( result > 0 ) {
            AVPacket packet;
            av_init_packet( &packet );
            packet.data = bitBucket;
            packet.size = result;

            if( video->codec->coded_frame->pts != AV_NOPTS_VALUE ) {
                packet.pts = av_rescale_q( video->codec->coded_frame->pts,
                    video->codec->time_base, video->time_base );
            }

            if( video->codec->coded_frame->key_frame )
                packet.flags |= PKT_FLAG_KEY;

            if( av_interleaved_write_frame( context, &packet ) < 0 ) {
                PyErr_SetString( PyExc_Exception, "Failed to write frame." );
                return NULL;
            }
        }
    }

    // Close format
    av_write_trailer( context );

    // Close file
    url_fclose( stream );

    free( bitBucket );
    takeVideoSource( NULL, &videoSource );
    takeAudioSource( NULL, &audioSource );

    if( videoSource.funcs ) {
        avcodec_close( video->codec );
        slice_free( RAMP_SIZE, ramp );
        slice_free( frameSize.x * frameSize.y * sizeof(rgba_f16), inputFrame.frameData );
        slice_free( interBufferSize, interBuffer );
        slice_free( outputBufferSize, outputBuffer );
        sws_freeContext( scaler );
        av_free( video );
    }

    if( audioSource.funcs ) {
        avcodec_close( audio->codec );
        slice_free( outSampleBufSize, outSampleBuf );
        slice_free( sizeof(float) * sampleCount * audioChannels, audioInputFrame.frameData );
        av_free( audio );
    }

    av_free( context );

    Py_RETURN_NONE;
}

