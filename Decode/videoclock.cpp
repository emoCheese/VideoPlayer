#include "videoclock.h"
#include <chrono>

VideoClock::VideoClock()
{
    reset();
}

void VideoClock::reset()
{
    clockPts_.store(0.0);
    baseSysTime_.store(nowSec());
}

void VideoClock::setSpeed(double speed)
{
    if (speed <= 0.0)
        speed = 1.0;
    speed_.store(speed);
}

void VideoClock::update(double pts)
{
    clockPts_.store(pts);
    baseSysTime_.store(nowSec());
}

double VideoClock::time() const
{
    double pts   = clockPts_.load();
    double base  = baseSysTime_.load();
    double speed = speed_.load();

    double elapsed = (nowSec() - base) * speed;
    return pts + elapsed;
}

double VideoClock::delay(double nextPts) const
{
    double curTime = time();
    double diff = nextPts - curTime;

    // 允许轻微负值，避免抖动
    if (diff < -0.05)
        return 0.0;

    return diff;
}

double VideoClock::nowSec()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(
               clock::now().time_since_epoch()
               ).count();
}

