
#include "VideoWidget.h"
#include <GL/gl.h>
#include <stdio.h>

static void invalidate( VideoWidget *widget ) {
    widget->update();
}

VideoWidget::VideoWidget( const QGLFormat &format, QWidget *parent ) : QGLWidget( format, parent ), _clockHolder(), _sourceHolder() {
    _context = widget_gl_new();
    widget_gl_set_invalidate_func( _context, (invalidate_func) invalidate, this );
}

VideoWidget::~VideoWidget() {
    widget_gl_free( _context );
}

void VideoWidget::setVideoSource( video_source *videoSource ) {
    widget_gl_set_video_source( _context, videoSource );
}

void VideoWidget::setPresentationClock( presentation_clock *presentationClock ) {
    widget_gl_set_presentation_clock( _context, presentationClock );
}

void VideoWidget::setDisplayWindow( box2i *displayWindow ) {
    widget_gl_set_display_window( _context, displayWindow );
}

void VideoWidget::getDisplayWindow( box2i *displayWindow ) {
    widget_gl_get_display_window( _context, displayWindow );
}

void VideoWidget::setPixelAspectRatio( float pixelAspectRatio ) {
    widget_gl_set_pixel_aspect_ratio( _context, pixelAspectRatio );
}

float VideoWidget::pixelAspectRatio() {
    return widget_gl_get_pixel_aspect_ratio( _context );
}

void VideoWidget::paintGL() {
    QSize mySize = size();
    v2i glSize = { mySize.width(), mySize.height() };

    widget_gl_draw( _context, glSize );
}

