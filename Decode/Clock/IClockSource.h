#ifndef ICLOCKSOURCE_H
#define ICLOCKSOURCE_H

class IClockSource {
public:
    virtual ~IClockSource() = default;
    virtual double now() const = 0;
    virtual void set(double pts) = 0;    // 设置时间
    virtual void pause(bool) = 0;

    // 倍速部分
    virtual void setSpeed(double speed) = 0;
    virtual double speed() const = 0;
    virtual void reset() = 0;
};


#endif // ICLOCKSOURCE_H
