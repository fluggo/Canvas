
#include "VideoWidget.h"
#include <GL/gl.h>

VideoWidget::VideoWidget(const QGLFormat &format, QWidget *parent) : QGLWidget(format, parent) {
}

void VideoWidget::paintGL() {
    glClearColor(1.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

