
%Module qt 0

%Import QtOpenGL/QtOpenGLmod.sip

class VideoWidget : QGLWidget {
%TypeHeaderCode
#include <VideoWidget.h>
%End

public:
    VideoWidget(const QGLFormat &format, QWidget *parent = 0);

protected:
    virtual void paintGL();
};

