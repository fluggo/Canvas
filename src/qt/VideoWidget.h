
#include "widget_gl.h"
#include <qobject.h>
#include <QGLWidget>

class VideoWidget : public QGLWidget {
    Q_OBJECT

public:
    VideoWidget( const QGLFormat &format, QWidget *parent = 0 );
    ~VideoWidget();
    void setVideoSource( video_source *videoSource );
    void setPresentationClock( presentation_clock *presentationClock );
    void setDisplayWindow( box2i *displayWindow );
    void getDisplayWindow( box2i *displayWindow );
    void setPixelAspectRatio( float pixelAspectRatio );
    float pixelAspectRatio();

    // BJC: I'd rather these be protected at worst, but right
    // now I don't know a better place to put them (they support the SIP code)
    PresentationClockHolder _clockHolder;
    VideoSourceHolder _sourceHolder;

protected:
    virtual void paintGL();

private:
    widget_gl_context *_context;
};

