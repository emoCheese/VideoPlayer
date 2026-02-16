#ifndef VIDEOCLOCK_H
#define VIDEOCLOCK_H
#include <atomic>

class VideoClock {
public:
    VideoClock();

    void reset();                             // start / seek  重置时钟（开始播放或跳转）
    void setSpeed(double speed = 1.0);       // 1.0 = normal

    void update(double pts);                 // after frame rendered 在帧渲染完成后调用
    double time() const;                     // 获取当前视频时间 (s)
    double delay(double nextPts) const;     // sleep time before next frame

private:
    static double nowSec();

private:
    std::atomic<double> clockPts_{0.0};     // last frame pts
    std::atomic<double> speed_{1.0};

    std::atomic<double> baseSysTime_{0.0};  // system time when clockPts was set
};

#endif // VIDEOCLOCK_H
