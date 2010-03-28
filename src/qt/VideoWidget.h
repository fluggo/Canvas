
#include <qobject.h>
#include <QGLWidget>

class VideoWidget : public QGLWidget {
    Q_OBJECT

public:
    VideoWidget(const QGLFormat &format, QWidget *parent = 0);

protected:
    virtual void paintGL();
};

