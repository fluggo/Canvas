
#include "framework.h"

using namespace Iex;
using namespace Imf;

static float gamma22ExpandFunc( float input ) {
    return powf( input, 2.2f );
}

static halfFunction<half> __gamma22( gamma22ExpandFunc, half( -2.0f ), half( 2.0f ) );

AVFileReader::AVFileReader( const char *filename )
    : _context( 0 ), _codecContext( NULL ), _codec( NULL ), _firstVideoStream( -1 ),
        _inputFrame( NULL ), _rgbFrame( NULL ), _rgbBuffer( NULL ) {
    int error;

    av_register_all();

    if( (error = av_open_input_file( &_context, filename, NULL, 0, NULL )) != 0 )
        throwErrnoExc( "Could not open the file (%T).", -error );

    if( (error = av_find_stream_info( _context )) < 0 )
        throwErrnoExc( "Could not find the stream info (%T).", -error );

    for( uint i = 0; i < _context->nb_streams; i++ ) {
        if( _context->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO ) {
            _firstVideoStream = i;
            break;
        }
    }

    if( _firstVideoStream == -1 )
        THROW( Iex::BaseExc, "Could not find a video stream." );

    _codecContext = _context->streams[_firstVideoStream]->codec;
    _codec = avcodec_find_decoder( _codecContext->codec_id );

    if( _codec == NULL )
        THROW( Iex::BaseExc, "Could not find a codec for the stream." );

    if( (error = avcodec_open( _codecContext, _codec )) < 0 )
        throwErrnoExc( "Could not open a codec (%T).", -error );

    if( (_inputFrame = avcodec_alloc_frame()) == NULL )
        throwErrnoExc( "Could not allocate input frame (%T).", ENOMEM );

    if( (_rgbFrame = avcodec_alloc_frame()) == NULL )
        throwErrnoExc( "Could not allocate output frame (%T).", ENOMEM );

    int byteCount = avpicture_get_size( PIX_FMT_YUV444P, _codecContext->width,
        _codecContext->height );

    if( (_rgbBuffer = (uint8_t*) av_malloc( byteCount )) == NULL )
        throwErrnoExc( "Could not allocate output frame buffer (%T).", ENOMEM );

    avpicture_fill( (AVPicture *) _rgbFrame, _rgbBuffer, PIX_FMT_YUV444P,
        _codecContext->width, _codecContext->height );

    // Rec. 601 weights courtesy of http://www.poynton.com/notes/colour_and_gamma/ColorFAQ.html
/*    _colorMatrix[0][0] = 1.0f / 219.0f;    // Y -> R
    _colorMatrix[0][1] = 0.0f;    // Pb -> R, and so on
    _colorMatrix[0][2] = 1.402f / 244.0f;
    _colorMatrix[1][0] = 1.0f / 219.0f;
    _colorMatrix[1][1] = -0.344136f / 244.0f;
    _colorMatrix[1][2] = -0.714136f / 244.0f;
    _colorMatrix[2][0] = 1.0f / 219.0f;
    _colorMatrix[2][1] = 1.772f / 244.0f;
    _colorMatrix[2][2] = 0.0f;*/

    // Naturally, this page disappeared soon after I referenced it, these are from intersil AN9717
    _colorMatrix[0][0] = 1.0f;
    _colorMatrix[0][1] = 0.0f;
    _colorMatrix[0][2] = 1.371f;
    _colorMatrix[1][0] = 1.0f;
    _colorMatrix[1][1] = -0.336f;
    _colorMatrix[1][2] = -0.698f;
    _colorMatrix[2][0] = 1.0f;
    _colorMatrix[2][1] = 1.732f;
    _colorMatrix[2][2] = 0.0f;

    _scaler = sws_getContext(
        _codecContext->width, _codecContext->height, _codecContext->pix_fmt,
        _codecContext->width, _codecContext->height, PIX_FMT_YUV444P, SWS_POINT,
        NULL, NULL, NULL );

    if( _scaler == NULL )
        throwErrnoExc( "Could not allocate scaler (%T).", ENOMEM );
}

AVFileReader::~AVFileReader() {
    if( _scaler != NULL ) {
        sws_freeContext( _scaler );
        _scaler = NULL;
    }

    if( _rgbBuffer != NULL ) {
        av_free( _rgbBuffer );
        _rgbBuffer = NULL;
    }

    if( _rgbFrame != NULL ) {
        av_free( _rgbFrame );
        _rgbFrame = NULL;
    }

    if( _inputFrame != NULL ) {
        av_free( _inputFrame );
        _inputFrame = NULL;
    }

    if( _codecContext != NULL ) {
        avcodec_close( _codecContext );
        _codecContext = NULL;
    }

    if( _context != NULL ) {
        av_close_input_file( _context );
        _context = NULL;
    }
}

void AVFileReader::GetFrame( int64_t frame, Array2D<Rgba> &array ) {
    frame = frame % _context->streams[_firstVideoStream]->duration;

    if( frame < 0 )
        frame += _context->streams[_firstVideoStream]->duration;

    if( av_seek_frame( _context, _firstVideoStream, frame % _context->streams[_firstVideoStream]->duration,
            AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD ) < 0 )
        THROW( Iex::BaseExc, "Could not seek to frame " << frame << "." );

    avcodec_flush_buffers( _codecContext );

    for( ;; ) {
        AVPacket packet;

        if( av_read_frame( _context, &packet ) < 0 )
            THROW( Iex::BaseExc, "Could not read the frame." );

        if( packet.stream_index != _firstVideoStream ) {
            av_free_packet( &packet );
            continue;
        }

        int gotPicture;

        avcodec_decode_video( _codecContext, _inputFrame, &gotPicture,
            packet.data, packet.size );

        if( !gotPicture ) {
            av_free_packet( &packet );
            continue;
        }

        AVFrame *frame = _rgbFrame;

#if DO_SCALE
        if( sws_scale( _scaler, _inputFrame->data, _inputFrame->linesize, 0,
                _codecContext->height, _rgbFrame->data, _rgbFrame->linesize ) < _codecContext->height ) {
            av_free_packet( &packet );
            THROW( Iex::BaseExc, "The image conversion failed." );
        }
#else
        frame = _inputFrame;
#endif

        // Now convert to halfs
        uint8_t *yplane = frame->data[0], *cbplane = frame->data[1], *crplane = frame->data[2];
        half a = 1.0f;
        const float __unbyte = 1.0f / 255.0f;

        for( int row = 0; row < _codecContext->height; row++ ) {
#if DO_SCALE
            for( int x = 0; x < _codecContext->width; x++ ) {
                float y = yplane[x] - 16.0f, cb = cbplane[x] - 128.0f, cr = crplane[x] - 128.0f;

                array[row][x].r = __gamma22( y * _colorMatrix[0][0] + cb * _colorMatrix[0][1] +
                    cr * _colorMatrix[0][2] );
                array[row][x].g = __gamma22( y * _colorMatrix[1][0] + cb * _colorMatrix[1][1] +
                    cr * _colorMatrix[1][2] );
                array[row][x].b = __gamma22( y * _colorMatrix[2][0] + cb * _colorMatrix[2][1] +
                    cr * _colorMatrix[2][2] );
                array[row][x].a = a;
            }
#else
            for( int x = 0; x < _codecContext->width / 4; x++ ) {
                float cb = cbplane[x] - 128.0f, cr = crplane[x] - 128.0f;

                float ccr = cb * _colorMatrix[0][1] + cr * _colorMatrix[0][2];
                float ccg = cb * _colorMatrix[1][1] + cr * _colorMatrix[1][2];
                float ccb = cb * _colorMatrix[2][1] + cr * _colorMatrix[2][2];

                for( int i = 0; i < 4; i++ ) {
                    float y = yplane[x * 4 + i];

                    array[row][x * 4 + i].r = __gamma22( (y * _colorMatrix[0][0] + ccr) * __unbyte );
                    array[row][x * 4 + i].g = __gamma22( (y * _colorMatrix[1][0] + ccg) * __unbyte );
                    array[row][x * 4 + i].b = __gamma22( (y * _colorMatrix[2][0] + ccb) * __unbyte );
                    array[row][x].a = a;
                }
            }
#endif

            yplane += frame->linesize[0];
            cbplane += frame->linesize[1];
            crplane += frame->linesize[2];
        }

        av_free_packet( &packet );
        return;
    }
}


