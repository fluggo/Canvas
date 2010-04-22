
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
        // Python people expect to pass a 4-tuple.
        box2i window;

        if( !PyArg_ParseTuple( a0, "iiii", &window.min.x, &window.min.y, &window.max.x, &window.max.y ) ) {
            sipIsErr = 1;
        }
        else if( box2i_isEmpty( &window ) ) {
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

        sipRes = Py_BuildValue( "(iiii)", window.min.x, window.min.y,
            window.max.x, window.max.y );
%End

    void setVideoSource( SIP_PYOBJECT );
%MethodCode
        bool result = takeVideoSource( a0, &sipCpp->_sourceHolder );

        if( !result )
            sipIsErr = 1;
        else
            sipCpp->setVideoSource( &sipCpp->_sourceHolder.source );
%End

    void setPresentationClock( SIP_PYOBJECT );
%MethodCode
        bool result = takePresentationClock( a0, &sipCpp->_clockHolder );

        if( !result )
            sipIsErr = 1;
        else
            sipCpp->setPresentationClock( &sipCpp->_clockHolder.source );
%End

    void setPixelAspectRatio( float pixelAspectRatio );
    float pixelAspectRatio();

    virtual QSize sizeHint() const;

protected:
    virtual void paintGL();
};

