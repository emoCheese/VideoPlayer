#include "videoclock.h"
#include <chrono>



void VideoClock::resetImpl() {
    clockPts_.store(0.0);
    baseSysTime_.store(nowSec());
}

void VideoClock::setSpeedImpl(double speed) {
    if (speed <= 0.0)
        speed = 1.0;
    speed_.store(speed);
}

void VideoClock::updateImpl(double pts) {
    clockPts_.store(pts);
    baseSysTime_.store(nowSec());
}

double VideoClock::timeImpl() const {
    double pts   = clockPts_.load();
    double base  = baseSysTime_.load();
    double speed = speed_.load();
    return pts + (nowSec() - base) * speed;
}

double VideoClock::delayImpl(double nextPts) const {
    double diff = nextPts - timeImpl();

    // ffplay 风格：允许轻微负值
    if (diff < -0.05)
        return 0.0;

    return diff;
}

double VideoClock::nowSec() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(
               clock::now().time_since_epoch()).count();
}
