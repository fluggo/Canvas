
#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include "framework.h"

using namespace Iex;
using namespace Imath;
using namespace Imf;

static PyObject *pysourceFuncs;

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
} py_obj_AVFileReader;

static float gamma22ExpandFunc( float input ) {
    return powf( input, 2.2f );
}

static halfFunction<half> __gamma22( gamma22ExpandFunc, half( -2.0f ), half( 2.0f ) );

static int
AVFileReader_init( py_obj_AVFileReader *self, PyObject *args, PyObject *kwds ) {
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

    for( uint i = 0; i < self->context->nb_streams; i++ ) {
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

    return 0;
}

static void
AVFileReader_dealloc( py_obj_AVFileReader *self ) {
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
AVFileReader_getFrame( py_obj_AVFileReader *self, int64_t frameIndex, RgbaFrame *frame ) {
    if( frameIndex < 0 || frameIndex > self->context->streams[self->firstVideoStream]->duration ) {
        // No result
        frame->currentDataWindow = Box2i( V2i(0, 0), V2i(-1, -1) );
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
            av_seek_frame( self->context, self->firstVideoStream, timestamp - frameDuration * 6, AVSEEK_FLAG_BACKWARD );
        }

        self->currentVideoFrame = frameIndex;
    }

    //avcodec_flush_buffers( self->codecContext );

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
        frame->currentDataWindow.min.x = std::max( 0, frame->currentDataWindow.min.x );
        frame->currentDataWindow.min.y = std::max( 0, frame->currentDataWindow.min.y );
        frame->currentDataWindow.max.x = std::min( self->codecContext->width - 1, frame->currentDataWindow.max.x );
        frame->currentDataWindow.max.y = std::min( self->codecContext->height - 1, frame->currentDataWindow.max.y );

        Box2i coordWindow = Box2i(
            frame->currentDataWindow.min - frame->fullDataWindow.min,
            frame->currentDataWindow.max - frame->fullDataWindow.min
        );

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

                        frame->frameData[row][x * 4 + i].r =
                            __gamma22( (y * self->colorMatrix[0][0] + ccr) * __unbyte );
                        frame->frameData[row][x * 4 + i].g =
                            __gamma22( (y * self->colorMatrix[1][0] + ccg) * __unbyte );
                        frame->frameData[row][x * 4 + i].b =
                            __gamma22( (y * self->colorMatrix[2][0] + ccb) * __unbyte );
                        frame->frameData[row][x * 4 + i].a = a;
                    }
                }
    #endif

                yplane += avFrame->linesize[0];
                cbplane += avFrame->linesize[1];
                crplane += avFrame->linesize[2];
            }
        }
        else if( self->codecContext->pix_fmt == PIX_FMT_YUV420P ) {
            uint8_t *yplane = avFrame->data[0], *cbplane = avFrame->data[1], *crplane = avFrame->data[2];
            half a = 1.0f;
            const float __unbyte = 1.0f / 255.0f;

            for( int y = coordWindow.min.y / 2; y <= coordWindow.max.y / 2; y++ ) {
                for( int x = coordWindow.min.x / 2; x <= coordWindow.max.x / 2; x++ ) {
                    float cb = cbplane[x] - 128.0f, cr = crplane[x] - 128.0f;

                    float ccr = cb * self->colorMatrix[0][1] + cr * self->colorMatrix[0][2];
                    float ccg = cb * self->colorMatrix[1][1] + cr * self->colorMatrix[1][2];
                    float ccb = cb * self->colorMatrix[2][1] + cr * self->colorMatrix[2][2];

                    for( int py = y * 2; py <= y * 2 + 1; py++ ) { for( int px = x * 2; px <= x * 2 + 1; px++ ) {
                        if( px < coordWindow.min.x || px > coordWindow.max.x || py < coordWindow.min.y || py > coordWindow.max.y )
                            continue;

                        float cy = yplane[avFrame->linesize[0] * py + px] - 16.0f;

                        frame->frameData[py][px].r =
                            __gamma22( (cy * self->colorMatrix[0][0] + ccr) * __unbyte );
                        frame->frameData[py][px].g =
                            __gamma22( (cy * self->colorMatrix[1][0] + ccg) * __unbyte );
                        frame->frameData[py][px].b =
                            __gamma22( (cy * self->colorMatrix[2][0] + ccb) * __unbyte );
                        frame->frameData[py][px].a = a;
                    } }
                }

                cbplane += avFrame->linesize[1];
                crplane += avFrame->linesize[2];
            }
        }
        else {
            frame->currentDataWindow = Box2i( V2i(0, 0), V2i(-1, -1) );
        }

        av_free_packet( &packet );
        return;
    }
}

static PyTypeObject py_type_AVFileReader = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.AVFileReader",    // tp_name
    sizeof(py_obj_AVFileReader)    // tp_basicsize
};

static VideoFrameSourceFuncs sourceFuncs = {
    0,
    (video_getFrameFunc) AVFileReader_getFrame
};

static PyObject *
AVFileReader_getFuncs( py_obj_AVFileReader */*self*/, void */*closure*/ ) {
    return pysourceFuncs;
}

static PyGetSetDef AVFileReader_getsetters[] = {
    { "_videoFrameSourceFuncs", (getter) AVFileReader_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PyObject *
AVFileReader_size( py_obj_AVFileReader *self ) {
    return Py_BuildValue( "(ii)", self->codecContext->width, self->codecContext->height );
}

static PyMethodDef AVFileReader_methods[] = {
    { "size", (PyCFunction) AVFileReader_size, METH_NOARGS,
        "Gets the frame size for this video." },
    { NULL }
};

void init_AVFileReader( PyObject *module ) {
    py_type_AVFileReader.tp_flags = Py_TPFLAGS_DEFAULT;
    py_type_AVFileReader.tp_new = PyType_GenericNew;
    py_type_AVFileReader.tp_dealloc = (destructor) AVFileReader_dealloc;
    py_type_AVFileReader.tp_init = (initproc) AVFileReader_init;
    py_type_AVFileReader.tp_getset = AVFileReader_getsetters;
    py_type_AVFileReader.tp_methods = AVFileReader_methods;

    if( PyType_Ready( &py_type_AVFileReader ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_AVFileReader );
    PyModule_AddObject( module, "AVFileReader", (PyObject *) &py_type_AVFileReader );

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );
}



