
#include "framework.h"
#include "clock.h"

#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <GL/gl.h>

#include <sstream>
#include <string>
#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <sys/timerfd.h>

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

static void drawFrame( void *rgb, int width, int height ) {
    glLoadIdentity();
    glViewport( 0, 0, width, height );
    glOrtho( 0, width, height, 0, -1, 1 );

    glClearColor( 0.0f, 1.0f, 0.0f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );

    glDrawPixels( width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb );
}

typedef struct {
    int num;
    int denom;
} rate;

typedef struct {
    IFrameSource *_source;
    int _timer;
    GMutex *_frameReadMutex;
    GCond *_frameReadCond;
    int _lastDisplayedFrame, _nextToRenderFrame;
    int readBuffer, writeBuffer, filled;
    rate frameRate;
    guint _timeoutSourceID;
    IPresentationClock *_clock;

    int64_t _presentationTime[4];
    Array2D<uint8_t[3]> _targets[4];
    float _rate;
} VideoWidgetInfo;

static gboolean
videoWidget_expose( GtkWidget *widget, GdkEventExpose *event, gpointer data ) {
    GdkGLContext *glcontext = gtk_widget_get_gl_context( widget );
    GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable( widget );

    VideoWidgetInfo *info = (VideoWidgetInfo*) g_object_get_data( G_OBJECT(widget), "__info" );

    if( !gdk_gl_drawable_gl_begin( gldrawable, glcontext ) )
        return FALSE;

    drawFrame( &info->_targets[info->readBuffer][0][0], 720, 480 );

    if( gdk_gl_drawable_is_double_buffered( gldrawable ) )
        gdk_gl_drawable_swap_buffers( gldrawable );
    else
        glFlush();

    gdk_gl_drawable_gl_end( gldrawable );

    return TRUE;
}

class Pulldown23RemovalFilter : public IFrameSource {
public:
    Pulldown23RemovalFilter( IFrameSource *source, int offset, bool oddFirst );

    virtual void GetFrame( int64_t frame, Array2D<Rgba> &array );

private:
    IFrameSource *_source;
    int _offset;
    bool _oddFirst;
};

int64_t
getFrameTime( rate *frameRate, int frame ) {
    return (int64_t) frame * INT64_C(1000000000) * (int64_t)(frameRate->denom) / (int64_t)(frameRate->num);
}

gpointer PlaybackThread( gpointer data ) {
    VideoWidgetInfo *info = (VideoWidgetInfo *) data;

    AVFileReader reader( "/home/james/Videos/Okra - 79b,100.avi" );
    Pulldown23RemovalFilter filter( &reader, 0, false );
    Array2D<Rgba> array( 480, 720 );

    for( ;; ) {
        int64_t startTime = info->_clock->getPresentationTime();
        g_mutex_lock( info->_frameReadMutex );
        if( info->filled == -1 )
            info->filled = 0;

        while( info->filled == 3 )
            g_cond_wait( info->_frameReadCond, info->_frameReadMutex );

        int nextFrame = info->_nextToRenderFrame;
        int writeBuffer = (info->writeBuffer = (info->writeBuffer + 1) & 3);
        g_mutex_unlock( info->_frameReadMutex );

//        printf( "Start rendering %d into %d...\n", nextFrame, writeBuffer );

        filter.GetFrame( nextFrame, array );

        // Convert the results to floating-point
        for( int y = 0; y < 480; y++ ) {
            for( int x = 0; x < 720; x++ ) {
                info->_targets[writeBuffer][479 - y][x][0] = (uint8_t) __gamma45( array[y][x].r );
                info->_targets[writeBuffer][479 - y][x][1] = (uint8_t) __gamma45( array[y][x].g );
                info->_targets[writeBuffer][479 - y][x][2] = (uint8_t) __gamma45( array[y][x].b );
            }
        }

        //usleep( 100000 );

        info->_presentationTime[writeBuffer] = getFrameTime( &info->frameRate, nextFrame );
        int64_t endTime = info->_clock->getPresentationTime();

        int64_t lastDuration = endTime - startTime;

        //printf( "Rendered frame %d into %d in %f presentation seconds...\n", _nextToRenderFrame, buffer,
        //    ((double) endTime - (double) startTime) / 1000000000.0 );
        //printf( "Presentation time %ld\n", info->_presentationTime[writeBuffer] );

        g_mutex_lock( info->_frameReadMutex );
        info->filled++;

        if( lastDuration > INT64_C(0) ) {
            while( getFrameTime( &info->frameRate, ++info->_nextToRenderFrame ) < endTime + lastDuration );
        }
        else if( lastDuration < INT64_C(0) ) {
            while( getFrameTime( &info->frameRate, --info->_nextToRenderFrame ) > endTime + lastDuration );
        }
        g_mutex_unlock( info->_frameReadMutex );

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

gboolean
playSingleFrame( gpointer data ) {
    GtkWidget *widget = (GtkWidget*) data;
    VideoWidgetInfo *info = (VideoWidgetInfo*) g_object_get_data( G_OBJECT(widget), "__info" );

    if( info->filled > 0 ) {
        g_mutex_lock( info->_frameReadMutex );
        int filled = info->filled;
        int readBuffer = (info->readBuffer = (info->readBuffer + 1) & 3);
        g_mutex_unlock( info->_frameReadMutex );

        if( filled != 0 ) {
            gdk_window_invalidate_rect( widget->window, &widget->allocation, FALSE );
            gdk_window_process_updates( widget->window, FALSE );

            //printf( "Painted %ld from %d...\n", info->_presentationTime[readBuffer], readBuffer );

            g_mutex_lock( info->_frameReadMutex );

            info->filled--;

            g_cond_signal( info->_frameReadCond );
            g_mutex_unlock( info->_frameReadMutex );
        }
    }

    int speedNum, speedDenom;
    info->_clock->getSpeed( &speedNum, &speedDenom );

    info->_timeoutSourceID = g_timeout_add(
        (1000 * info->frameRate.denom * abs(speedDenom)) / (info->frameRate.num * abs(speedNum)),
        playSingleFrame, data );
    return FALSE;
}

gboolean
keyPressHandler( GtkWidget *widget, GdkEventKey *event, gpointer userData ) {
    VideoWidgetInfo *info = (VideoWidgetInfo*) g_object_get_data( G_OBJECT((GtkWidget*) userData), "__info" );

    if( info->_timeoutSourceID != 0 ) {
        g_source_remove( info->_timeoutSourceID );
        info->_timeoutSourceID = 0;
    }

    int speedNum, speedDenom;
    info->_clock->getSpeed( &speedNum, &speedDenom );


    switch( gdk_keyval_to_unicode( event->keyval ) ) {
        case (guint32) 'l':
            if( speedDenom != 1 )
                speedDenom >>= 1;
            else
                speedNum <<= 1;
            break;

        case (guint32) 'j':
            if( speedNum != 1 )
                speedNum >>= 1;
            else
                speedDenom <<= 1;
            break;
    }

    ((SystemPresentationClock*) info->_clock)->play( speedNum, speedDenom );

    g_mutex_lock( info->_frameReadMutex );
    info->filled = -1;
    info->readBuffer = 3;
    info->writeBuffer = 3;
    g_cond_signal( info->_frameReadCond );
    g_mutex_unlock( info->_frameReadMutex );

    playSingleFrame( (GtkWidget*) userData );

    return TRUE;
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

int
main( int argc, char *argv[] ) {
    gtk_init( &argc, &argv );
    gtk_gl_init( &argc, &argv );

    if( !g_thread_supported() )
        g_thread_init( NULL );

    GdkGLConfig *glconfig;

    glconfig = gdk_gl_config_new_by_mode ( (GdkGLConfigMode) (GDK_GL_MODE_RGB    |
                                        GDK_GL_MODE_DEPTH  |
                                        GDK_GL_MODE_DOUBLE));
    if (glconfig == NULL)
    {
        g_print ("*** Cannot find the double-buffered visual.\n");
        g_print ("*** Trying single-buffered visual.\n");

        /* Try single-buffered visual */
        glconfig = gdk_gl_config_new_by_mode ((GdkGLConfigMode) (GDK_GL_MODE_RGB   |
                                        GDK_GL_MODE_DEPTH));
        if (glconfig == NULL)
        {
            g_print ("*** No appropriate OpenGL-capable visual found.\n");
            exit (1);
        }
    }

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

    GtkWidget *window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_title( GTK_WINDOW(window), "boogidy boogidy" );

    GtkWidget *drawingArea = gtk_drawing_area_new();
    gtk_widget_set_size_request( drawingArea, 720, 480 );

    gtk_widget_set_gl_capability( drawingArea,
                                glconfig,
                                NULL,
                                TRUE,
                                GDK_GL_RGBA_TYPE );

    int h = 480, w = 720;

    SystemPresentationClock clock;
    clock.set( 16, 1, 5000LL * 1000000000LL * 1001LL / 24000LL );

    VideoWidgetInfo info;
    info._clock = &clock;
    info._frameReadMutex = g_mutex_new();
    info._frameReadCond = g_cond_new();
    info._nextToRenderFrame = 5000;
    info.frameRate.num = 24000;
    info.frameRate.denom = 1001;
    info.filled = -1;
    info.readBuffer = 3;
    info.writeBuffer = 3;
    info._targets[0].resizeErase( h, w );
    info._targets[1].resizeErase( h, w );
    info._targets[2].resizeErase( h, w );
    info._targets[3].resizeErase( h, w );

    g_object_set_data( G_OBJECT(drawingArea), "__info", &info );
    g_signal_connect( G_OBJECT(drawingArea), "expose_event", G_CALLBACK(videoWidget_expose), NULL );
    g_signal_connect( G_OBJECT(window), "key-press-event", G_CALLBACK(keyPressHandler), drawingArea );

    gtk_container_add( GTK_CONTAINER(window), drawingArea );
    gtk_widget_show( drawingArea );

    gtk_widget_show( window );

    g_timeout_add( 0, playSingleFrame, drawingArea );
    g_thread_create( PlaybackThread, &info, FALSE, NULL );

    gtk_main();
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

