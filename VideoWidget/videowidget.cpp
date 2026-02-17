#include <glad/glad.h>
#include "VideoWidget.h"

VideoWidget::VideoWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

void VideoWidget::initializeGL()
{
    gladLoadGL();
    renderer.init();
}

void VideoWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    renderer.resize(w, h);
}

void VideoWidget::paintGL()
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    QMutexLocker locker(&mutex);
    if (hasFrame) {
        renderer.uploadNV12(frameBuf.data(), frameW, frameH);
    }
    renderer.draw();
}
