#include "videowidget.h"
#include <QDebug>
#include <cstring>

VideoWidget::VideoWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

VideoWidget::~VideoWidget()
{
    makeCurrent();
    delete[] frameBuf;
    frameBuf = nullptr;

    if (program) {
        program->release();
        delete program;
        program = nullptr;
    }

    if (texY) {
        glDeleteTextures(1, &texY);
        texY = 0;
    }
    if (texUV) {
        glDeleteTextures(1, &texUV);
        texUV = 0;
    }
    doneCurrent();
}

/* ================== 接收 RTSP 解码帧 ================== */
void VideoWidget::onFrameArrived(uchar *nv12, int w, int h)
{
    if (!nv12 || w <= 0 || h <= 0) {
        qWarning() << "Invalid frame data received";
        return;
    }

    QMutexLocker locker(&frameMutex);

    int size = w * h * 3 / 2;
    if (!frameBuf || w != lastFrameW || h != lastFrameH) {
        delete[] frameBuf;
        frameBuf = new uint8_t[size];
        lastFrameW = w;
        lastFrameH = h;
        needToUpdateTextures = true;  // 尺寸改变时需要更新纹理
    }

    memcpy(frameBuf, nv12, size);
    frameW = w;
    frameH = h;
    hasVideoFrame.store(true, std::memory_order_release);
    update();
}

/* ================== OpenGL ================== */
void VideoWidget::initializeGL()
{
    initializeOpenGLFunctions();

    if (!glGetError()) {
        qInfo() << "OpenGL initialized successfully";
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    initShader();
    initGeometry();
    initTextures();
}

void VideoWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void VideoWidget::paintGL()
{
    QMutexLocker locker(&frameMutex);
    if (!hasVideoFrame.load(std::memory_order_acquire))
        return;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 更新纹理数据
    if (needToUpdateTextures || frameW != lastFrameW || frameH != lastFrameH) {
        // Y 平面
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texY);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,  // 使用GL_R8替代GL_LUMINANCE (已废弃)
                     frameW, frameH,
                     0, GL_RED, GL_UNSIGNED_BYTE,
                     frameBuf);

        // UV 平面
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texUV);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8,  // 使用GL_RG8替代GL_LUMINANCE_ALPHA (已废弃)
                     frameW / 2, frameH / 2,
                     0, GL_RG, GL_UNSIGNED_BYTE,
                     frameBuf + frameW * frameH);

        lastFrameW = frameW;
        lastFrameH = frameH;
        needToUpdateTextures = false;
    } else {
        // 只需更新纹理内容，不重新分配纹理内存
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texY);
        glTexSubImage2D(GL_TEXTURE_2D, 0,
                        0, 0, frameW, frameH,
                        GL_RED, GL_UNSIGNED_BYTE,
                        frameBuf);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texUV);
        glTexSubImage2D(GL_TEXTURE_2D, 0,
                        0, 0, frameW / 2, frameH / 2,
                        GL_RG, GL_UNSIGNED_BYTE,
                        frameBuf + frameW * frameH);
    }

    program->bind();
    program->setUniformValue("texY", 0);
    program->setUniformValue("texUV", 1);

    vao.bind();
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    vao.release();

    program->release();

    // 检查渲染错误
    checkGLError("paintGL");
}

/* ================== 初始化 ================== */
void VideoWidget::initTextures()
{
    glGenTextures(1, &texY);
    glBindTexture(GL_TEXTURE_2D, texY);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &texUV);
    glBindTexture(GL_TEXTURE_2D, texUV);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    checkGLError("initTextures");
}

void VideoWidget::initGeometry()
{
    float vertices[] = {
        // pos      // tex
        -1, -1,    0, 1,  // 左下角
        1, -1,    1, 1,  // 右下角
        1,  1,    1, 0,  // 右上角
        -1,  1,    0, 0   // 左上角
    };

    vao.create();
    vbo.create();

    vao.bind();
    vbo.bind();
    vbo.allocate(vertices, sizeof(vertices));

    program->enableAttributeArray(0);   // 启用位置属性数组
    program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));  // 设置位置属性的数据格式（2个浮点数，步长为4个float）

    program->enableAttributeArray(1);       // 启用纹理坐标属性数组
    program->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));  // 设置纹理坐标属性的数据格式

    vbo.release();
    vao.release();

    checkGLError("initGeometry");
}

void VideoWidget::initShader()
{
    program = new QOpenGLShaderProgram(this);
    program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSrc());
    program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSrc());

    if (!program->link()) {
        qCritical() << "Shader program linking failed:" << program->log();
    } else {
        qInfo() << "Shader program linked successfully";
    }

    checkGLError("initShader");
}

void VideoWidget::checkGLError(const QString& operation)
{
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        qCritical() << "OpenGL error in" << operation << ":" << error;
    }
}

/* ================== Shader ================== */
QString VideoWidget::vertexShaderSrc() const
{
    return R"(
        #version 330 core
        layout (location = 0) in vec2 position;
        layout (location = 1) in vec2 texcoord;
        out vec2 vTex;
        void main() {
            gl_Position = vec4(position, 0.0, 1.0);
            vTex = texcoord;
        }
    )";
}

QString VideoWidget::fragmentShaderSrc() const
{
    return R"(
        #version 330 core
        in vec2 vTex;
        out vec4 FragColor;

        uniform sampler2D texY;
        uniform sampler2D texUV;

        void main() {
            float y = texture(texY, vTex).r;
            vec2 uv = texture(texUV, vTex).rg - vec2(0.5, 0.5);

            float r = y + 1.402 * uv.y;
            float g = y - 0.344 * uv.x - 0.714 * uv.y;
            float b = y + 1.772 * uv.x;

            // 执行YUV到RGB的颜色空间转换
            FragColor = vec4(clamp(vec3(r, g, b), 0.0, 1.0), 1.0);
        }
    )";
}
