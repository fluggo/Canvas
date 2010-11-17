
%Module qt 0

%Import QtOpenGL/QtOpenGLmod.sip

class VideoWidget : QGLWidget {
%TypeHeaderCode
#include <VideoWidget.h>
%End

public:
    VideoWidget( const QGLFormat &format, QWidget *parent = 0 );

    void setDisplayWindow( SIP_PYTUPLE );
%MethodCode
        box2i window;

        if( !py_parse_box2i( a0, &window ) ) {
            sipIsErr = 1;
        }
        else if( box2i_is_empty( &window ) ) {
            PyErr_SetString( PyExc_Exception, "An empty window was passed to setDisplayWindow." );
            sipIsErr = 1;
        }
        else {
            sipCpp->setDisplayWindow( &window );
        }
%End

    SIP_PYTUPLE getDisplayWindow();
%MethodCode
        box2i window;
        sipCpp->getDisplayWindow( &window );

        sipRes = py_make_box2i( &window );
%End

    void setVideoSource( SIP_PYOBJECT );
%MethodCode
        bool result = py_video_take_source( a0, &sipCpp->_sourceHolder );

        if( !result )
            sipIsErr = 1;
        else
            sipCpp->setVideoSource( sipCpp->_sourceHolder.source );
%End

    void setPresentationClock( SIP_PYOBJECT );
%MethodCode
        bool result = py_presentation_clock_take_source( a0, &sipCpp->_clockHolder );

        if( !result )
            sipIsErr = 1;
        else
            sipCpp->setPresentationClock( &sipCpp->_clockHolder.source );
%End

    void setPixelAspectRatio( float pixelAspectRatio );
    float pixelAspectRatio();

    void setRenderingIntent( float renderingIntent );
    float renderingIntent();

    virtual QSize sizeHint() const;

protected:
    virtual void paintGL();
};

