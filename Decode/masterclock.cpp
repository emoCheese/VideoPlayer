#include "masterclock.h"
#include <chrono>
#include <spdlog/spdlog.h>

void MasterClock::loop()
{
    while (running_.load(std::memory_order_relaxed)) {
        VideoFrame frame;
        if (!queue_.pop(frame)) {
            spdlog::info("MasterClock: queue closed, exit");
            break;
        }
        // ---------------- pause 处理 ----------------
        if (paused_.load(std::memory_order_relaxed)) {
            auto pause_start = nowSec();
            // 等待直到 resume
            while (paused_.load() && running_.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(2));

            // 时间补偿
            double pause_end = nowSec();
            frame_timer_ += (pause_end - pause_start);
        }

        const double pts = frame.pts;
        const int serial = frame.serial;

        // ---------------- serial 切换处理 ----------------
        if (serial != current_serial_) {
            spdlog::info("MasterClock: serial switch {} -> {}",
                         current_serial_, serial);

            current_serial_ = serial;

            // 重置时钟基准
            frame_timer_ = nowSec() - pts;
            last_pts_ = pts;
            last_duration_ = 1.0 / 25.0;

            if (callback_) {
                callback_(std::make_shared<VideoFrame>(std::move(frame)));
            }
            continue;
        }

        // ---------------- 计算 duration ----------------
        double duration = pts - last_pts_;
        if (duration <= 0.0 || duration > max_frame_duration_)
            duration = last_duration_;

        last_duration_ = duration;

        // ---------------- 获取主时钟 ----------------
        double master_time = nowSec() - frame_timer_;

        // 未来音频同步时：
        // master_time = getMasterTime();

        double delay = pts - master_time;

        // ---------------- 丢帧 ----------------
        if (delay < drop_threshold_) {
            spdlog::debug("MasterClock: drop frame pts={:.6f} delay={:.6f}",
                          pts, delay);
            last_pts_ = pts;
            continue;
        }

        // ---------------- 等待播放时间 ----------------
        if (delay > 0) {

            if (delay > fine_wait_threshold_) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(
                        static_cast<int>((delay - 0.001) * 1e6)));
            }

            while (running_.load()) {
                master_time = nowSec() - frame_timer_;
                if (pts - master_time <= 0)
                    break;
                std::this_thread::yield();
            }
        }

        // ---------------- 渲染 ----------------
        if (callback_) {
            callback_(std::make_shared<VideoFrame>(std::move(frame)));
        }

        last_pts_ = pts;
    }
}
