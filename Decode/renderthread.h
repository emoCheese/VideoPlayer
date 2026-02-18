#ifndef RENDERTHREAD_H
#define RENDERTHREAD_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <thread>
#include <atomic>


class VideoPlayer;

class RenderThread {
public:
    explicit RenderThread(VideoPlayer* player);
    ~RenderThread();

    void start();
    void stop();

private:
    void run();
    bool initGL();
    void renderLoop();

private:
    VideoPlayer* player_ = nullptr;
    std::thread thread_;
    std::atomic<bool> abort_{false};

    GLFWwindow* window_ = nullptr;
    double frame_timer_ = 0.0;
};


#endif // RENDERTHREAD_H
