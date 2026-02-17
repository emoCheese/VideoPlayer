#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QOpenGLWidget>
#include <QMutex>
#include <vector>

#include "VideoRendererCore.h"

class VideoWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
    VideoRendererCore renderer;

    QMutex mutex;
    std::vector<uint8_t> frameBuf;
    int frameW = 0;
    int frameH = 0;
    bool hasFrame = false;
};


#endif // VIDEOWIDGET_H
