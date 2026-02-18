#include "nv12_renderer.h"
#include "framequeue.h"
#include <vector>
#include <iostream>

// ================= shader =================

static const char* vs_src = R"(#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTex;
out vec2 vTex;
void main() {
    vTex = aTex;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* fs_src = R"(#version 330 core
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

    FragColor = vec4(r, g, b, 1.0);
}
)";

static GLuint compile(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::cerr << log << std::endl;
    }
    return s;
}

// ================= impl =================

NV12Renderer::~NV12Renderer()
{
    release();
}

bool NV12Renderer::init()
{
    initShader();
    initGeometry();

    glGenTextures(1, &texY_);
    glGenTextures(1, &texUV_);

    return true;
}

void NV12Renderer::initShader()
{
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);

    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glLinkProgram(prog_);

    glDeleteShader(vs);
    glDeleteShader(fs);

    glUseProgram(prog_);
    glUniform1i(glGetUniformLocation(prog_, "texY"), 0);
    glUniform1i(glGetUniformLocation(prog_, "texUV"), 1);
}

void NV12Renderer::initGeometry()
{
    float quad[] = {
        -1, -1,  0, 1,
        1, -1,  1, 1,
        -1,  1,  0, 0,
        1,  1,  1, 0,
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void NV12Renderer::allocTextures(int w, int h)
{
    texW_ = w;
    texH_ = h;

    glBindTexture(GL_TEXTURE_2D, texY_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0,
                 GL_RED, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, texUV_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8,
                 w / 2, h / 2, 0,
                 GL_RG, GL_UNSIGNED_BYTE, nullptr);

    for (GLuint t : {texY_, texUV_}) {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

void NV12Renderer::upload(const VideoFrame* f)
{
    if (f->width != texW_ || f->height != texH_) {
        allocTextures(f->width, f->height);
    }

    const uint8_t* y  = f->data.data();
    const uint8_t* uv = y + f->width * f->height;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texY_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    f->width, f->height,
                    GL_RED, GL_UNSIGNED_BYTE, y);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texUV_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    f->width / 2, f->height / 2,
                    GL_RG, GL_UNSIGNED_BYTE, uv);
}

void NV12Renderer::draw()
{
    glUseProgram(prog_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void NV12Renderer::release()
{
    if (texY_)  glDeleteTextures(1, &texY_);
    if (texUV_) glDeleteTextures(1, &texUV_);
    if (vbo_)   glDeleteBuffers(1, &vbo_);
    if (vao_)   glDeleteVertexArrays(1, &vao_);
    if (prog_)  glDeleteProgram(prog_);

    texY_ = texUV_ = vao_ = vbo_ = prog_ = 0;
}

