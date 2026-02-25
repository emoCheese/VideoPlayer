#ifndef EXTERNALCLOCK_H
#define EXTERNALCLOCK_H
#include "IClockSource.h"
#include <atomic>
#include <chrono>
#include <mutex>

class ExternalClock : public IClockSource {
public:
    ExternalClock()
    {
        reset();
    }

    double now() const override
    {
        if (paused_.load())
            return paused_pts_;
        return getSystemTime() - base_;
    }

    void set(double pts) override
    {
        base_ = getSystemTime() - pts;
        paused_pts_ = pts;
    }

    void pause(bool p) override
    {
        if (p && !paused_) {
            paused_pts_ = now();
            paused_ = true;
        }
        else if (!p && paused_) {
            base_ = getSystemTime() - paused_pts_;
            paused_ = false;
        }
    }

private:
    static double getSystemTime()
    {
        using Clock = std::chrono::steady_clock;
        return std::chrono::duration<double>(
                   Clock::now().time_since_epoch()).count();
    }

    void reset()
    {
        base_ = getSystemTime();
    }

private:
    mutable std::mutex mutex_;
    std::atomic<double> base_ = 0.0;
    std::atomic<double> paused_pts_ = 0.0;
    std::atomic_bool paused_ = false;
};
#endif // EXTERNALCLOCK_H
