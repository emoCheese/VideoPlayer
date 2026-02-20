#include "ClockBase.h"

void VideoClock::reset()
{
    pts_ = 0.0;
    lastUpdated_ = nowSec();
    speed_ = 1.0;
    paused_ = false;
    serial_ = 0;
}

void VideoClock::syncTo(double pts, int serial)
{
    pts_ = pts;
    lastUpdated_ = nowSec();
    serial_ = serial;
}

void VideoClock::setSpeed(double speed)
{
    if (speed <= 0.0)
        speed = 1.0;

    // 保持连续性
    pts_ = time();
    lastUpdated_ = nowSec();

    speed_ = speed;
}

void VideoClock::pause()
{
    if (paused_) return;

    pts_ = time();
    paused_ = true;
}

void VideoClock::resume()
{
    if (!paused_) return;

    lastUpdated_ = nowSec();
    paused_ = false;
}

double VideoClock::time() const
{
    if (paused_)
        return pts_;
    double now = nowSec();
    return pts_ + (now - lastUpdated_) * speed_;
}

double VideoClock::delay(double framePts) const
{
    double diff = framePts - time();
    if (diff < -0.1)
        return 0.0;
    return diff;
}

int VideoClock::serial() const { return serial_; }
