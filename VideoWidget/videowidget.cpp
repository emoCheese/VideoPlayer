#include "videowidget.h"
#include "VideoPlayer.h"
#include "ClockBase.h"
#include <QDebug>
#include <spdlog/spdlog.h>

// ==================== 顶点着色器 ====================
// 功能：将顶点位置传递给 GPU，并传递纹理坐标给片段着色器
static const char* vs_src = R"(#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTex;
out vec2 vTex;
void main() {
    vTex = aTex;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";


// ==================== 片段着色器 ====================
// 功能：采样 YUV 纹理，转换为 RGB 颜色
// YUV -> RGB 转换矩阵 (BT.601 标准)
// R = Y + 1.402 * V
// G = Y - 0.344 * U - 0.714 * V
// B = Y + 1.772 * U
static const char* fs_src = R"(#version 330 core
in vec2 vTex;
out vec4 FragColor;

uniform sampler2D texY;
uniform sampler2D texUV;

void main() {
    float y = texture(texY, vTex).r;  // 从 Y 纹理采样（红色通道）
    vec2 uv = texture(texUV, vTex).rg - vec2(0.5, 0.5); // 从 UV 纹理采样（R=U, G=V），并转换到 [-0.5, 0.5] 范围

    float r = y + 1.402 * uv.y;
    float g = y - 0.344 * uv.x - 0.714 * uv.y;
    float b = y + 1.772 * uv.x;

    FragColor = vec4(r, g, b, 1.0);
}
)";

VideoWidget::VideoWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
}

VideoWidget::~VideoWidget()
{
    makeCurrent();
    glDeleteTextures(1, &texY_);
    glDeleteTextures(1, &texUV_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    doneCurrent();
}

void VideoWidget::setVideoPlayer(VideoPlayer* player)
{
    player_ = player;
}

void VideoWidget::initializeGL()
{
    initializeOpenGLFunctions();    // 初始化 OpenGL 函数指针（必须首先调用）

    // 编译并链接着色器程序
    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, vs_src);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, fs_src);
    program_.link();

    initGLResources();  // 初始化 VAO/VBO/纹理

    glClearColor(0, 0, 0, 1);
}

void VideoWidget::initGLResources()
{
    // 定义全屏四边形的顶点和纹理坐标
    // 格式：x, y, u, v
    // 使用 GL_TRIANGLE_STRIP 绘制顺序：0->1->2->3
    float vertices[] = {
        // pos      // tex
        -1, -1,     0, 1,  // 左下 (纹理坐标翻转，因为 OpenGL 纹理原点在左下)
        1, -1,     1, 1,  // 右下
        -1,  1,     0, 0,  // 左上
        1,  1,     1, 0,  // 右上
    };

    // 生成 VAO 和 VBO
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    // 绑定并填充数据
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // 配置顶点属性 (location = 0, 位置)
    // 参数：索引，分量数，类型，是否归一化，步长，偏移
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 配置顶点属性 (location = 1, 纹理坐标)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);

    // 解绑 VAO（保持状态）
    glBindVertexArray(0);

    // ==================== 创建纹理 ====================
    glGenTextures(1, &texY_);
    glGenTextures(1, &texUV_);

    // 配置 Y 纹理
    glBindTexture(GL_TEXTURE_2D, texY_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // 缩小过滤
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);  // 放大过滤
    // 注意：此时不分配内存，等 uploadFrame() 时根据实际视频尺寸分配

    // 配置 UV 纹理
    glBindTexture(GL_TEXTURE_2D, texUV_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);  // 解绑
}

void VideoWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void VideoWidget::renderStep(std::shared_ptr<VideoFrame> frame)
{
    makeCurrent();
    uploadFrame(frame.get());
    hasFrame_ = true;
    update();
}

void VideoWidget::onFrameReady(std::shared_ptr<VideoFrame> frame)
{
    renderStep(frame);
}

// ==================== 上传帧数据到纹理 ====================
/**
 * @brief 将 NV12 帧数据上传到 OpenGL 纹理
 *
 * NV12 内存布局：
 * [YYYYYYYY]  <- Y 平面 (width * height)
 * [UVUVUVUV]  <- UV 平面 (width * height / 2)，交错存储
 */
void VideoWidget::uploadFrame(VideoFrame* frame)
{
    if (!frame)
        return;

    // 如果视频尺寸变化，重新分配纹理内存
    if (frame->width != texWidth_ || frame->height != texHeight_)
    {
        texWidth_  = frame->width;
        texHeight_ = frame->height;

        // Y 纹理：GL_R8 格式，单通道
        glBindTexture(GL_TEXTURE_2D, texY_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                     texWidth_, texHeight_,
                     0, GL_RED, GL_UNSIGNED_BYTE, nullptr);

        // UV 纹理：GL_RG8 格式，双通道
        // 尺寸是 Y 的一半（宽和高都除以 2）
        glBindTexture(GL_TEXTURE_2D, texUV_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8,
                     texWidth_/2, texHeight_/2,
                     0, GL_RG, GL_UNSIGNED_BYTE, nullptr);
    }

    // 获取 Y 和 UV 平面指针
    // NV12 格式：Y 平面在前，UV 平面紧随其后
    const uint8_t* yPlane  = frame->data.data();
    const uint8_t* uvPlane = yPlane + texWidth_ * texHeight_;

    // 设置像素存储对齐为 1 字节（避免对齐问题）
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // 更新 Y 纹理数据
    glBindTexture(GL_TEXTURE_2D, texY_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    texWidth_, texHeight_,
                    GL_RED, GL_UNSIGNED_BYTE, yPlane);

    // 更新 UV 纹理数据
    glBindTexture(GL_TEXTURE_2D, texUV_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    texWidth_/2, texHeight_/2,
                    GL_RG, GL_UNSIGNED_BYTE, uvPlane);
}

// ==================== 绘制帧 ====================
void VideoWidget::paintGL()
{
    // 如果没有有效帧，只清屏
    if (!hasFrame_)
    {
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    // 清屏
    glClear(GL_COLOR_BUFFER_BIT);

    // 绑定着色器程序
    program_.bind();

    // ==================== 绑定 Y 纹理 ====================
    glActiveTexture(GL_TEXTURE0);          // 激活纹理单元 0
    glBindTexture(GL_TEXTURE_2D, texY_);   // 绑定 Y 纹理
    program_.setUniformValue("texY", 0);   // 告诉着色器 texY 使用单元 0

    // ==================== 绑定 UV 纹理 ====================
    glActiveTexture(GL_TEXTURE1);          // 激活纹理单元 1
    glBindTexture(GL_TEXTURE_2D, texUV_);  // 绑定 UV 纹理
    program_.setUniformValue("texUV", 1);  // 告诉着色器 texUV 使用单元 1

    // 绑定 VAO 并绘制
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);  // 绘制 4 个顶点（2 个三角形）

    // 释放着色器
    program_.release();
}
