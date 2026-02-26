#ifndef EXTERNALCLOCK_H
#define EXTERNALCLOCK_H
#include "IClockSource.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <spdlog/spdlog.h>

class ExternalClock : public IClockSource {
public:
    ExternalClock()
    {
        const double now = getSystemTime();
        base_time_.store(now, std::memory_order_relaxed);
        offset_.store(0.0, std::memory_order_relaxed);
        paused_.store(false, std::memory_order_relaxed);

        SPDLOG_INFO("ExternalClock initialized at {:.6f}", now);
    }

    // =====================================
    // 当前时间
    // =====================================
    double now() const override
    {
        const bool paused = paused_.load(std::memory_order_acquire);

        const double offset = offset_.load(std::memory_order_acquire);

        if (paused)
            return offset;

        const double base = base_time_.load(std::memory_order_acquire);
        const double system = getSystemTime();

        return offset + (system - base);
    }

    // =====================================
    // 设置逻辑时间（重新锚定）
    // =====================================
    void set(double pts) override
    {
        const double system = getSystemTime();

        // 先设置基准时间
        base_time_.store(system, std::memory_order_release);

        // 再设置偏移
        offset_.store(pts, std::memory_order_release);

        SPDLOG_INFO("ExternalClock::set pts={:.6f}", pts);
    }

    // =====================================
    // 暂停 / 恢复
    // =====================================
    void pause(bool p) override
    {
        const bool current = paused_.load(std::memory_order_acquire);

        if (p && !current) {
            const double current_pts = now();
            offset_.store(current_pts, std::memory_order_release);
            paused_.store(true, std::memory_order_release);

            SPDLOG_DEBUG("ExternalClock paused at {:.6f}", current_pts);
        }
        else if (!p && current) {
            const double system = getSystemTime();
            base_time_.store(system, std::memory_order_release);
            paused_.store(false, std::memory_order_release);

            SPDLOG_DEBUG("ExternalClock resumed");
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
    // 系统时间锚点
    std::atomic<double> base_time_{0.0};

    // 当前逻辑时间偏移
    std::atomic<double> offset_{0.0};

    // 暂停状态
    std::atomic<bool> paused_{false};
};
#endif // EXTERNALCLOCK_H
