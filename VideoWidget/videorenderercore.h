#ifndef VIDEORENDERERCORE_H
#define VIDEORENDERERCORE_H

#include <GL/gl.h>
#include <cstdint>
#include <string>

namespace  {
const char* VERT_SHADER = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTex;

out vec2 vTex;

void main()
{
    vTex = aTex;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* FRAG_SHADER = R"(
#version 330 core
in vec2 vTex;
out vec4 FragColor;

uniform sampler2D texY;
uniform sampler2D texUV;

void main()
{
    float y = texture(texY, vTex).r;
    vec2 uv = texture(texUV, vTex).rg - vec2(0.5, 0.5);

    float r = y + 1.402 * uv.y;
    float g = y - 0.344 * uv.x - 0.714 * uv.y;
    float b = y + 1.772 * uv.x;

    FragColor = vec4(r, g, b, 1.0);
}
)";
}

class VideoRendererCore {
public:
    VideoRendererCore() = default;
    ~VideoRendererCore();

    bool init();
    void resize(int w, int h);

    // NV12: Y plane + interleaved UV
    void uploadNV12(const uint8_t* data, int w, int h);
    void draw();
    void release();

private:
    void initShader();
    void initGeometry();
    void initTextures(int w, int h);


    std::string vertexShaderSrc() const;
    std::string fragmentShaderSrc() const;

private:
    GLuint program = 0;

    GLuint vao = 0;
    GLuint vbo = 0;

    GLuint texY = 0;
    GLuint texUV = 0;

    int videoW = 0;
    int videoH = 0;
};

#endif // VIDEORENDERERCORE_H
