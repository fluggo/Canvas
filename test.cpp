
#include "framework.h"
#include "clock.h"

#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Button.H>
#include <FL/gl.h>
#include <sstream>
#include <string>
#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <sys/timerfd.h>

#define SAFE_DELETE(x)        do { if( x ) { delete x; x = 0; } } while( 0 )

using namespace Iex;
using namespace Imf;


//static float gamma22Func( float input ) {
//    return powf( input, 2.2 );
//}

// (16 / 255) ^ 2.2
const float __gamma22Base = 0.002262953f;

// (235 / 255) ^ 2.2 - __gamma22Base
const float __gamma22Extent = 0.835527791f - __gamma22Base;
const float __gamma22Fixer = 1.0f / __gamma22Extent;
const float __gammaCutoff = 21.0f / 255.0f;

inline float clamppowf( float x, float y ) {
    if( x < 0.0f )
        return 0.0f;

    if( x > 1.0f )
        return 1.0f;

    return powf( x, y );
}

inline float clampf( float x ) {
    return (x < 0.0f) ? 0.0f : ((x > 1.0f) ? 1.0f : x);
}

static float gamma45Func( float input ) {
    return Imath::clamp( powf( input, 0.45f ) * 255.0f, 0.0f, 255.0f );
}

static halfFunction<half> __gamma45( gamma45Func, half( -256.0f ), half( 256.0f ) );

class Pulldown23RemovalFilter : public IFrameSource {
public:
    Pulldown23RemovalFilter( IFrameSource *source, int offset, bool oddFirst );

    virtual void GetFrame( int64_t frame, Array2D<Rgba> &array );

private:
    IFrameSource *_source;
    int _offset;
    bool _oddFirst;
};

class VideoWidget : public Fl_Gl_Window {
public:
    VideoWidget( IPresentationClock *clock, int x, int y, int w, int h, const char *label = 0 );
    void draw() { do_draw(); }

    void play( float rate ) {
        pthread_t thread;
        pthread_create( &thread, NULL, _playbackThread, this );

        _rate = rate;
        Fl::add_timeout( rate, _frameCallback, this );
    }

    ~VideoWidget() {
    }

private:
    IFrameSource *_source;
    int _timer;
    pthread_mutex_t _frameReadMutex;
    pthread_cond_t _frameReadCond;
    int _lastDisplayedFrame, _nextToRenderFrame;
    int _currentReadBuffer, _filled;
    IPresentationClock *_clock;

    int64_t _presentationTime[4];
    Array2D<uint8_t[3]> _targets[4];
    void do_draw();
    float _rate;

    static void *_playbackThread( void *p ) {
        return ((VideoWidget *) p)->PlaybackThread();
    }

    int64_t getFrameTime( int frame ) {
        return (int64_t) frame * INT64_C(1000000000) * INT64_C(1001) / INT64_C(24000);
    }

    void *PlaybackThread() {
//        AVFileReader reader( "SMPTE test pattern.avi" );
        AVFileReader reader( "/home/james/Videos/Okra - 79b,100.avi" );
        Pulldown23RemovalFilter filter( &reader, 0, false );
        Array2D<Rgba> array( 480, 720 );
        int64_t frameDuration = getFrameTime( 1 ) - getFrameTime( 0 );
        int buffer = 0;

        for( ;; ) {
            int64_t startTime = _clock->getPresentationTime();
            pthread_mutex_lock( &_frameReadMutex );

            while( _filled == 3 )
                pthread_cond_wait( &_frameReadCond, &_frameReadMutex );

            pthread_mutex_unlock( &_frameReadMutex );

            //printf( "Start rendering %d into %d...\n", _nextToRenderFrame, buffer );

            filter.GetFrame( _nextToRenderFrame, array );

            // Convert the results to floating-point
            for( int y = 0; y < 480; y++ ) {
                for( int x = 0; x < 720; x++ ) {
/*                    _video->target[479 - y][x][0] = clamppowf( array[y][x].r, 0.45f );
                    _video->target[479 - y][x][1] = clamppowf( array[y][x].g, 0.45f );
                    _video->target[479 - y][x][2] = clamppowf( array[y][x].b, 0.45f );*/
                    _targets[buffer][479 - y][x][0] = (uint8_t) __gamma45( array[y][x].r );
                    _targets[buffer][479 - y][x][1] = (uint8_t) __gamma45( array[y][x].g );
                    _targets[buffer][479 - y][x][2] = (uint8_t) __gamma45( array[y][x].b );
                }
            }

            //usleep( 100000 );

            _presentationTime[buffer] = getFrameTime( _nextToRenderFrame );
            int64_t endTime = _clock->getPresentationTime();

            pthread_mutex_lock( &_frameReadMutex );
            _filled++;
            pthread_mutex_unlock( &_frameReadMutex );

            int64_t lastDuration = endTime - startTime;

            //printf( "Rendered frame %d into %d in %f presentation seconds...\n", _nextToRenderFrame, buffer,
            //    ((double) endTime - (double) startTime) / 1000000000.0 );
            //printf( "Presentation time %ld\n", _presentationTime[buffer] );

            if( lastDuration > INT64_C(0) ) {
                while( getFrameTime( ++_nextToRenderFrame ) < endTime + lastDuration );
            }
            else if( lastDuration < INT64_C(0) ) {
                while( getFrameTime( --_nextToRenderFrame ) > endTime + lastDuration );
            }

            buffer = (buffer + 1) & 3;

/*            std::stringstream filename;
            filename << "rgba" << i++ << ".exr";

            Header header( 720, 480, 40.0f / 33.0f );

            RgbaOutputFile file( filename.str().c_str(), header, WRITE_RGBA );
            file.setFrameBuffer( &array[0][0], 1, 720 );
            file.writePixels( 480 );

            puts( filename.str().c_str() );*/
        }

        return NULL;
    }

    void Frame() {
        if( _filled != 0 ) {
            _currentReadBuffer = (_currentReadBuffer + 1) & 3;

            redraw();

            //printf( "Painted %ld from %d...\n", _presentationTime[_currentReadBuffer], _currentReadBuffer );

            pthread_mutex_lock( &_frameReadMutex );

            _filled--;

            pthread_cond_signal( &_frameReadCond );
            pthread_mutex_unlock( &_frameReadMutex );
        }

        Fl::repeat_timeout( _rate, _frameCallback, this );
    }

    static void _frameCallback( void *ptr ) {
        ((VideoWidget*) ptr)->Frame();
    }
};


VideoWidget::VideoWidget( IPresentationClock *clock, int x, int y, int w, int h, const char *label )
    : Fl_Gl_Window( x, y, w, h, label ), _nextToRenderFrame( 5000 ), _currentReadBuffer( 3 ), _filled( 0 ), _clock( clock ) {

    pthread_mutex_init( &_frameReadMutex, NULL );
    pthread_cond_init( &_frameReadCond, NULL );

    _targets[0].resizeErase( h, w );
    _targets[1].resizeErase( h, w );
    _targets[2].resizeErase( h, w );
    _targets[3].resizeErase( h, w );
}

void VideoWidget::do_draw() {
//    printf( "Drawing\n" );

//    if( !valid() ) {
        glLoadIdentity();
        glViewport( 0, 0, w(), h() );
        glOrtho( 0, w(), h(), 0, -1, 1 );
    //}

    glClearColor( 0.0f, 1.0f, 0.0f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );

    glDrawPixels( 720, 480, GL_RGB, GL_UNSIGNED_BYTE, &_targets[_currentReadBuffer][0][0] );
}

/*
    <source name='scene7'>
        <avsource file='Okra Principle - 7 (good take).avi' duration='5000' durationUnits='frames'>
            <stream type='video' number='0' gamma='0.45' colorspace='Rec601' />
            <stream type='audio' number='1' audioMap='stereo' />
        </avsource>
    </source>
    <clip name='shot7a/cam1/take1' source='scene7'>
        <version label='1' start='56' startUnits='frames' duration='379' durationUnits='frames'>
            <pulldown style='23' offset='3' />
        </version>
    </clip>
    <source name='scene7fostex'>
        <avsource file='scene7fostex.wav' duration='5000000' durationUnits='samples'>
            <stream type='audio' number='0' audioMap='custom'>
                <audioMap sourceChannel='left' targetChannel='center' />
            </stream>
        </avsource>
    </source>
    <clip name='shot7a/fostex/take1' source='scene7fostex'>
        <version label='1' start='5000' startUnits='ms' duration='10000' durationUnits='ms'/>
    </clip>
    <take name='scene7/shot7a/take1'>
        <version label='1'>
            <clip name='shot7a/cam1/take1' start='0' startUnits='frames' />
            <clip name='shot7a/fostex/take1' start='-56' startUnits='ms' />
        </version>
    </take>
    <timeline>
    </timeline>
*/


class MainWindow : public Fl_Window {
public:
    MainWindow( int width, int height ) : Fl_Window( width, height ) {
        _transportControls = new Fl_Pack( 0, 0, 600, 30 );
        _transportControls->type( Fl_Pack::HORIZONTAL );
        _playButton = new Fl_Button( 0, 0, 50, 30, "@>" );
        _playButton->callback( _playCallback, this );
        _pauseButton = new Fl_Button( 0, 0, 50, 30, "@||" );
        _pauseButton->callback( _pauseCallback, this );
        _transportControls->end();

        _video = new VideoWidget( &_clock, 0, 50, 720, 480 );
        end();

        _video->play( 1.0f / 24.0f );
        _clock.set( -1, 1, 5000LL * 1000000000LL * 1001LL / 24000LL );
    }

protected:

private:
    Fl_Pack *_transportControls;
    Fl_Button *_playButton, *_pauseButton;
    SystemPresentationClock _clock;
    VideoWidget *_video;

    void PlayCallback( Fl_Widget *widget ) {
    }

    void PauseCallback( Fl_Widget *widget ) {
    }

    static void _playCallback( Fl_Widget *widget, void *data ) {
        ((MainWindow *) data)->PlayCallback( widget );
    }

    static void _pauseCallback( Fl_Widget *widget, void *data ) {
        ((MainWindow *) data)->PauseCallback( widget );
    }
};

int
main( int argc, char *argv[] ) {
    struct timespec res;
    clock_getres( CLOCK_MONOTONIC, &res );

    printf( "Clock resolution: %ld seconds, %ld nanoseconds\n", res.tv_sec, res.tv_nsec );

    Fl::gl_visual( FL_RGB );
    Fl::lock();

//    Fl::add_idle( updateCallback, NULL );

/*    AVFileReader reader( "/home/james/Desktop/Demo2.avi" );
    Array2D<Rgba> array( 480, 720 );
    int i = 0;

    while( reader.ReadFrame( array ) ) {
        std::stringstream filename;
        filename << "rgba" << i++ << ".exr";

        //Header header( 720, 480, 40.0f / 33.0f );

        //RgbaOutputFile file( filename.str().c_str(), header, WRITE_RGBA );
        //file.setFrameBuffer( &array[0][0], 1, 720 );
        //file.writePixels( 480 );

        puts( filename.str().c_str() );
    }*/


    MainWindow window( 800, 600 );
    window.show( argc, argv );

    return Fl::run();
}


Pulldown23RemovalFilter::Pulldown23RemovalFilter( IFrameSource *source, int offset, bool oddFirst )
    : _source( source ), _offset( offset ), _oddFirst( oddFirst ) {
}

void Pulldown23RemovalFilter::GetFrame( int64_t frame, Array2D<Rgba> &array ) {
    // Cadence offsets:

    // 0 AA BB BC CD DD (0->0, 1->1, 3->4), (2->2b3a)
    // 1 BB BC CD DD EE (0->0, 2->3, 3->4), (1->1b2a)
    // 2 BC CD DD EE FF (1->2, 2->3, 3->4), (0->0b1a)
    // 3 CD DD EE FF FG (0->1, 1->2, 2->3), (3->4b5a) (same as 4 with 1st frame discarded)
    // 4 DD EE FF FG GH (0->0, 1->1, 2->2), (3->3b4a)

    int frameOffset;

    if( _offset == 4 )
        frameOffset = (frame + 3) & 3;
    else
        frameOffset = (frame + _offset) & 3;

    int64_t baseFrame = ((frame + _offset) >> 2) * 5 - _offset;

    // Solid frames
    if( frameOffset == 0 ) {
        _source->GetFrame( baseFrame, array );
    }
    else if( frameOffset == 1 ) {
        _source->GetFrame( baseFrame + 1, array );
    }
    else if( frameOffset == 3 ) {
        _source->GetFrame( baseFrame + 4, array );
    }
    else {
        // Mixed fields
        _source->GetFrame( baseFrame + 2, array );

        Array2D<Rgba> temp( 480, 720 );
        _source->GetFrame( baseFrame + 3, temp );

        for( int i = (_oddFirst ? 0 : 1); i < 480; i += 2 )
            memcpy( array[i], temp[i], 720 * sizeof(Rgba) );
    }
}

