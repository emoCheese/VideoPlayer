#include "RenderThread.h"
#include "VideoPlayer.h"
#include "nv12_renderer.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <GLFW/glfw3.h>
// NV12 → OpenGL（Y/UV 双纹理 + shader）完整实现

static double nowSec()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(
               clock::now().time_since_epoch()).count();
}

RenderThread::RenderThread(VideoPlayer* player)
    : player_(player)
{
}

RenderThread::~RenderThread()
{
    stop();
}

void RenderThread::start()
{
    abort_ = false;
    thread_ = std::thread(&RenderThread::run, this);
}

void RenderThread::stop()
{
    abort_ = true;
    if (thread_.joinable())
        thread_.join();
}

void RenderThread::run()
{
    if (!initGL())
        return;

    frame_timer_ = nowSec();   // 对齐当前时间

    renderLoop();

    glfwDestroyWindow(window_);
    glfwTerminate();
}

bool RenderThread::initGL()
{
    if (!glfwInit()) {
        std::cerr << "glfwInit failed\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(1280, 720, "Video", nullptr, nullptr);
    if (!window_) {
        std::cerr << "create window failed\n";
        return false;
    }

    glfwMakeContextCurrent(window_);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "gladLoadGLLoader failed\n";
        return false;
    }

    glViewport(0, 0, 1280, 720);
    glClearColor(0, 0, 0, 1);

    return true;
}

void RenderThread::renderLoop()
{
    NV12Renderer renderer;
    renderer.init();

    while (!abort_ && !glfwWindowShouldClose(window_)) {

        VideoFrame* frame = nullptr;
        if (!player_->peekVideoFrame(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        double pts = frame->pts * av_q2d({1, AV_TIME_BASE});
        VideoClock& clock = player_->clock();

        double delay = clock.delay(pts);

        // frame drop
        if (delay < -0.1) {
            player_->popVideoFrame();
            continue;
        }

        if (delay > 0.0) {
            std::this_thread::sleep_for(
                std::chrono::duration<double>(delay));
        }

        glClear(GL_COLOR_BUFFER_BIT);

        renderer.upload(frame);
        renderer.draw();

        glfwSwapBuffers(window_);
        glfwPollEvents();

        clock.update(pts);
        player_->popVideoFrame();
    }

    renderer.release();
}


