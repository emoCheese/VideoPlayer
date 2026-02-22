#ifndef MASTERCLOCK_H
#define MASTERCLOCK_H

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include "framequeue.h"


class MasterClock {
public:
    using Callback = std::function<void(std::shared_ptr<VideoFrame>)>;
    using Clock = std::chrono::steady_clock;

    MasterClock(FrameQueue<VideoFrame>& queue) : queue_(queue) {}

    void start(Callback cb) {
        callback_ = std::move(cb);
        running_ = true;
        thread_ = std::thread(&MasterClock::loop, this);
    }

    void stop() {
        running_ = false;
        if (thread_.joinable())
            thread_.join();
    }

    void pause(bool p) {
        paused_ = p;
    }

private:
    void loop();

    static double nowSec() {
        return std::chrono::duration<double>(
                   Clock::now().time_since_epoch()).count();
    }

    FrameQueue<VideoFrame>& queue_;
    Callback callback_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::thread thread_;

    // 帧定时器状态
    double frame_timer_ = 0.0;
    double last_pts_ = 0.0;
    double last_duration_ = 1.0 / 25.0;  // 默认 25fps
    int current_serial_ = -1;

    // 常数阈值
    const double max_frame_duration_ = 1.0;      // 最大允许帧间隔（秒）
    const double drop_threshold_ = -0.05;        // 丢帧阈值（落后超过 50ms 则丢帧）
    const double fine_wait_threshold_ = 0.002;   // 精细等待阈值（2ms）
};

#endif // MASTERCLOCK_H
