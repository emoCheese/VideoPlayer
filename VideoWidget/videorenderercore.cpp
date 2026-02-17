#include <glad/glad.h>
#include "VideoRendererCore.h"
#include <iostream>

static GLuint compileShader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::cerr << "Shader error: " << log << std::endl;
    }
    return s;
}

VideoRendererCore::~VideoRendererCore()
{
    release();
}

bool VideoRendererCore::init()
{
    initShader();
    initGeometry();
    return true;
}

void VideoRendererCore::initShader()
{
    GLuint vs = compileShader(GL_VERTEX_SHADER, VERT_SHADER);       // 顶点着色器
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, FRAG_SHADER);     // 片段着色器

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "texY"), 0);
    glUniform1i(glGetUniformLocation(program, "texUV"), 1);
}

void VideoRendererCore::initGeometry()
{
    float vertices[] = {
        // pos      // tex
        -1.f, -1.f, 0.f, 1.f,
        1.f, -1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 0.f,
        1.f,  1.f, 1.f, 0.f,
    };

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

void VideoRendererCore::initTextures(int w, int h)
{
    if (!texY) glGenTextures(1, &texY);
    if (!texUV) glGenTextures(1, &texUV);

    glBindTexture(GL_TEXTURE_2D, texY);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h,
                 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, texUV);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG, w / 2, h / 2,
                 0, GL_RG, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    videoW = w;
    videoH = h;
}

void VideoRendererCore::uploadNV12(const uint8_t* data, int w, int h)
{
    if (w != videoW || h != videoH) {
        initTextures(w, h);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texY);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, 0, w, h,
                    GL_RED, GL_UNSIGNED_BYTE, data);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texUV);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, 0, w / 2, h / 2,
                    GL_RG, GL_UNSIGNED_BYTE,
                    data + w * h);
}

void VideoRendererCore::draw()
{
    glUseProgram(program);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void VideoRendererCore::resize(int, int)
{
    // 可留空或处理投影
}

void VideoRendererCore::release()
{
    if (texY) glDeleteTextures(1, &texY);
    if (texUV) glDeleteTextures(1, &texUV);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (program) glDeleteProgram(program);

    texY = texUV = vao = vbo = program = 0;
}

