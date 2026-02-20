#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QTimer>
#include <atomic>

struct VideoFrame;
class VideoPlayer;
class VideoClock;

/**
 * @brief 视频渲染控件
 *
 * 继承关系：
 * - QOpenGLWidget: 提供 OpenGL 上下文和窗口部件功能
 * - QOpenGLFunctions_3_3_Core: 提供 OpenGL 3.3 Core Profile 的函数访问
 *
 * 线程模型：
 * - 所有 OpenGL 操作必须在 GUI 线程（渲染线程）中执行
 * - 通过 renderStep() 由外部定时器驱动
 */
class VideoWidget
    : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget();

    // 设置关联的播放器和时钟对象
    void setVideoPlayer(VideoPlayer* player);
    void setClock(VideoClock* clock);

    // 外部调用：用于时钟驱动渲染（由定时器或主循环调用）
    void renderStep();
    inline void renderStep(std::shared_ptr<VideoFrame>);

public slots:

    void onFrameReady(std::shared_ptr<VideoFrame> frame);

protected:
    // QOpenGLWidget 的三个核心虚函数
    void initializeGL() override;   // OpenGL 资源初始化（只调用一次）
    void resizeGL(int w, int h) override;  // 窗口大小变化时调用
    void paintGL() override;        // 每帧渲染时调用

private:
    void initGLResources();         // 初始化 VAO/VBO/纹理
    void uploadFrame(VideoFrame* frame);  // 上传视频帧数据到 GPU 纹理

private:
    VideoPlayer* player_ = nullptr;  // 播放器控制器（提供帧数据）

    QOpenGLShaderProgram program_;   // 着色器程序（顶点 + 片段）

    // OpenGL 对象 ID
    GLuint vao_ = 0;  // 顶点数组对象
    GLuint vbo_ = 0;  // 顶点缓冲对象

    GLuint texY_  = 0;  // Y 平面纹理
    GLuint texUV_ = 0;  // UV 平面纹理

    int texWidth_  = 0;   // 当前纹理宽度
    int texHeight_ = 0;   // 当前纹理高度

    std::atomic<bool> hasFrame_{false};  // 标记是否有有效帧（原子操作，线程安全）
};

#endif // VIDEOWIDGET_H
