#ifndef NV12_RENDERER_H
#define NV12_RENDERER_H

#include <cstdint>
#include <glad/glad.h>

struct VideoFrame;

class NV12Renderer {
public:
    NV12Renderer() = default;
    ~NV12Renderer();

    bool init();                       // OpenGL 已 ready
    void upload(const VideoFrame* f);  // NV12 → texture
    void draw();                       // fullscreen quad
    void release();

private:
    void initShader();
    void initGeometry();
    void allocTextures(int w, int h);

private:
    GLuint prog_ = 0;
    GLuint vao_  = 0;
    GLuint vbo_  = 0;

    GLuint texY_  = 0;
    GLuint texUV_ = 0;

    int texW_ = 0;
    int texH_ = 0;
};

#endif // NV12_RENDERER_H
