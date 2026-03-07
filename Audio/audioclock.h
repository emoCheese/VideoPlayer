#ifndef AUDIOCLOCK_H
#define AUDIOCLOCK_H

#include "IClockSource.h"
#include <SDL3/SDL.h>
#include <mutex>
class AudioClock : public IClockSource
{
public:

    void set(double pts) override
    {
        std::lock_guard lock(mutex_);

        pts_ = pts;
        last_system_time_ = system_now();
    }

    double now() const override
    {
        std::lock_guard lock(mutex_);

        if (paused_)
            return pts_;

        return pts_ + (system_now() - last_system_time_);
    }

    void pause(bool p) override
    {
        std::lock_guard lock(mutex_);

        if (p && !paused_) {
            pts_ = now();
            paused_ = true;
        }
        else if (!p && paused_) {
            last_system_time_ = system_now();
            paused_ = false;
        }
    }

private:

    static double system_now()
    {
        using Clock = std::chrono::steady_clock;
        return std::chrono::duration<double>(
                   Clock::now().time_since_epoch()).count();
    }

    double pts_ = 0.0;
    double last_system_time_ = 0.0;

    bool paused_ = false;

    mutable std::mutex mutex_;
};
#endif // AUDIOCLOCK_H
