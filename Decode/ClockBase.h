#ifndef CLOCKBASE_H
#define CLOCKBASE_H
#include <chrono>
#include <atomic>

class VideoClock
{
public:
    VideoClock() { reset(); }

    void reset();

    // 同步到某个 pts（首帧 / seek）
    void syncTo(double pts, int serial);

    void setSpeed(double speed);

    void pause();

    void resume();

    double time() const;

    double delay(double framePts) const;

    int serial() const;

    static double nowSec()
    {
        using clock = std::chrono::steady_clock;
        return std::chrono::duration<double>(
                   clock::now().time_since_epoch()).count();
    }


private:
    double pts_ = 0.0;           // 最近同步的 pts
    double lastUpdated_ = 0.0;   // 同步时的系统时间
    double speed_ = 1.0;

    bool paused_ = false;
    int serial_ = 0;
};


#endif // CLOCKBASE_H
