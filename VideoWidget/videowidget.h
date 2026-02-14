#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMutex>
#include <QTimer>

class VideoWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget();

public slots:
    // Thread 解码完成后调用
    void onFrameArrived(uchar* nv12, int w, int h);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void initShader();
    void initGeometry();
    void initTextures();
    void checkGLError(const QString& operation);

    QString vertexShaderSrc() const;
    QString fragmentShaderSrc() const;

private:
    // OpenGL
    QOpenGLShaderProgram* program = nullptr;
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};

    GLuint texY = 0;
    GLuint texUV = 0;

    // Frame buffer (NV12)
    QMutex frameMutex;
    uint8_t* frameBuf = nullptr;
    int frameW = 0;
    int frameH = 0;
    int lastFrameW = 0;  // 记录上一帧尺寸，避免不必要的内存重分配
    int lastFrameH = 0;

    // 是否有效视频帧
    std::atomic<bool> hasVideoFrame{false};

    // 性能优化标志
    bool needToUpdateTextures = true;
};

#endif // VIDEOWIDGET_H
