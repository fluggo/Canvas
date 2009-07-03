
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
} py_obj_FFVideoReader;

/*static float gamma22ExpandFunc( float input ) {
    return powf( input, 2.2f );
}*/

static half gamma22[65536];

/*static halfFunction<half> __gamma22( gamma22ExpandFunc, half( -2.0f ), half( 2.0f ) );*/

static int
FFVideoReader_init( py_obj_FFVideoReader *self, PyObject *args, PyObject *kwds ) {
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
FFVideoReader_dealloc( py_obj_FFVideoReader *self ) {
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
FFVideoReader_getFrame( py_obj_FFVideoReader *self, int64_t frameIndex, RgbaFrame *frame ) {
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
            uint8_t *yplane, *cbplane, *crplane;

            printf( "Allocating...\n" );
            rgba_f32 *tempRow = slice_alloc( sizeof(rgba_f32) * self->codecContext->width );

            if( !tempRow ) {
                printf( "Failed to allocate row\n" );
                box2i_setEmpty( &frame->currentDataWindow );
                av_free_packet( &packet );
                return;
            }

            for( int row = coordWindow.min.y; row <= coordWindow.max.y; row++ ) {
                yplane = avFrame->data[0] + (row * avFrame->linesize[0]);
                cbplane = avFrame->data[1] + (row * avFrame->linesize[1]);
                crplane = avFrame->data[2] + (row * avFrame->linesize[2]);

                for( int x = coordWindow.min.x / 4; x <= coordWindow.max.x / 4; x++ ) {
                    float cb = cbplane[x] - 128.0f, cr = crplane[x] - 128.0f;

                    float ccr = cb * self->colorMatrix[0][1] + cr * self->colorMatrix[0][2];
                    float ccg = cb * self->colorMatrix[1][1] + cr * self->colorMatrix[1][2];
                    float ccb = cb * self->colorMatrix[2][1] + cr * self->colorMatrix[2][2];

                    for( int i = 0; i < 4; i++ ) {
                        int px = x * 4 + i;
                        float y = yplane[px] - 16.0f;

                        tempRow[px].r = y * self->colorMatrix[0][0] + ccr;
                        tempRow[px].g = y * self->colorMatrix[1][0] + ccg;
                        tempRow[px].b = y * self->colorMatrix[2][0] + ccb;
                        tempRow[px].a = 1.0f;
                    }
                }

                half *out = &frame->frameData[row * frame->stride + coordWindow.min.x].r;

                half_convert_from_float_fast( (float*)(tempRow + coordWindow.min.x), out,
                    4 * (coordWindow.max.x - coordWindow.min.x + 1) );
                half_lookup( gamma22, out, out,
                    4 * (coordWindow.max.x - coordWindow.min.x + 1) );
            }

            printf( "Freeing...\n" );
            slice_free( sizeof(rgba_f32) * self->codecContext->width, tempRow );
        }
        else if( self->codecContext->pix_fmt == PIX_FMT_YUV420P ) {
            uint8_t *restrict yplane = avFrame->data[0], *restrict cbplane = avFrame->data[1], *restrict crplane = avFrame->data[2];
            rgba *restrict frameData = frame->frameData;

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
                            float in[4] = {
                                cy * self->colorMatrix[0][0] + ccr,
                                cy * self->colorMatrix[1][0] + ccg,
                                cy * self->colorMatrix[2][0] + ccb,
                                1.0f
                            };

                            half *out = &frameData[(py + pyi * i) * frame->stride + px].r;

                            half_convert_from_float_fast( in, out, 4 );
                            half_lookup( gamma22, out, out, 4 );
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
    (video_getFrameFunc) FFVideoReader_getFrame
};

static PyObject *pyVideoSourceFuncs;

static PyObject *
FFVideoReader_getFuncs( py_obj_FFVideoReader *self, void *closure ) {
    return pyVideoSourceFuncs;
}

static PyGetSetDef FFVideoReader_getsetters[] = {
    { "_videoFrameSourceFuncs", (getter) FFVideoReader_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PyObject *
FFVideoReader_size( py_obj_FFVideoReader *self ) {
    return Py_BuildValue( "(ii)", self->codecContext->width, self->codecContext->height );
}

static PyMethodDef FFVideoReader_methods[] = {
    { "size", (PyCFunction) FFVideoReader_size, METH_NOARGS,
        "Gets the frame size for this video." },
    { NULL }
};

static PyTypeObject py_type_FFVideoReader = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.FFVideoReader",    // tp_name
    sizeof(py_obj_FFVideoReader),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) FFVideoReader_dealloc,
    .tp_init = (initproc) FFVideoReader_init,
    .tp_getset = FFVideoReader_getsetters,
    .tp_methods = FFVideoReader_methods
};

NOEXPORT void init_AVFileReader( PyObject *module ) {
    float *f = malloc( sizeof(float) * 65536 );

    for( int i = 0; i < 65536; i++ )
        gamma22[i] = (uint16_t) i;

    half_convert_to_float( gamma22, f, 65536 );

    for( int i = 0; i < 65536; i++ )
        f[i] = powf( f[i] / 255.0f, 2.2f );

    half_convert_from_float( f, gamma22, 65536 );

    free( f );

    if( PyType_Ready( &py_type_FFVideoReader ) < 0 )
        return;

    Py_INCREF( &py_type_FFVideoReader );
    PyModule_AddObject( module, "FFVideoReader", (PyObject *) &py_type_FFVideoReader );

    pyVideoSourceFuncs = PyCObject_FromVoidPtr( &videoSourceFuncs, NULL );
}



