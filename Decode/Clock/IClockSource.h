#ifndef ICLOCKSOURCE_H
#define ICLOCKSOURCE_H

class IClockSource {
public:
    virtual ~IClockSource() = default;
    virtual double now() const = 0;
    virtual void set(double pts) = 0;    // 设置时间
    virtual void pause(bool) = 0;
};


#endif // ICLOCKSOURCE_H
