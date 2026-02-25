#ifndef VIDEOCLOCK_H
#define VIDEOCLOCK_H

#include "IClockSource.h"
#include <atomic>
#include <chrono>
#include <mutex>

#pragma once
#include <atomic>
#include <chrono>
#include <spdlog/spdlog.h>

class VideoClock : public IClockSource {
public:
    VideoClock() {
        const double now = getSystemTime();
        base_time_.store(now, std::memory_order_relaxed);
        pts_.store(0.0, std::memory_order_relaxed);
        paused_.store(false, std::memory_order_relaxed);

        spdlog::info("VideoClock initialized at {:.6f}", now);
    }

    // ================================
    // 获取当前时钟时间
    // ================================
    double now() const override
    {
        const bool paused = paused_.load(std::memory_order_acquire);
        const double pts = pts_.load(std::memory_order_acquire);

        if (paused)
            return pts;

        const double base = base_time_.load(std::memory_order_acquire);
        const double system = getSystemTime();

        return pts + (system - base);
    }

    // ================================
    // 设置当前 PTS
    // ================================
    void set(double new_pts) override
    {
        const double system = getSystemTime();

        // 先更新 base_time，再更新 pts
        base_time_.store(system, std::memory_order_release);
        pts_.store(new_pts, std::memory_order_release);

        // spdlog::debug("VideoClock::set pts={:.6f}", new_pts);
    }

    // ================================
    // 暂停 / 恢复
    // ================================
    void pause(bool p) override
    {
        const bool current = paused_.load(std::memory_order_acquire);

        if (p && !current) {
            // 暂停：冻结时间
            const double current_pts = now();
            pts_.store(current_pts, std::memory_order_release);
            paused_.store(true, std::memory_order_release);

            spdlog::debug("VideoClock paused at {:.6f}", current_pts);
        }
        else if (!p && current) {
            // 恢复：重新建立基准
            const double system = getSystemTime();
            base_time_.store(system, std::memory_order_release);
            paused_.store(false, std::memory_order_release);

            spdlog::debug("VideoClock resumed");
        }
    }

private:
    static double getSystemTime()
    {
        using Clock = std::chrono::steady_clock;
        return std::chrono::duration<double>(
                   Clock::now().time_since_epoch()).count();
    }

private:
    // 当前视频时间（逻辑时间）
    std::atomic<double> pts_{0.0};

    // 对应系统时间基准
    std::atomic<double> base_time_{0.0};

    // 是否暂停
    std::atomic<bool> paused_{false};
};
#endif // VIDEOCLOCK_H
