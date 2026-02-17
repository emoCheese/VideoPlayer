#ifndef VIDEOCLOCK_H
#define VIDEOCLOCK_H
#include <atomic>


template <typename Impl>
class ClockBase {
public:
    void reset() {
        impl().resetImpl();
    }

    void setSpeed(double speed) {
        impl().setSpeedImpl(speed);
    }

    void update(double pts) {
        impl().updateImpl(pts);
    }

    double time() const {
        return impl().timeImpl();
    }

    double delay(double nextPts) const {
        return impl().delayImpl(nextPts);
    }

protected:
    Impl& impl() {
        return static_cast<Impl&>(*this);
    }

    const Impl& impl() const {
        return static_cast<const Impl&>(*this);
    }
};


class VideoClock : public ClockBase<VideoClock> {
public:
    VideoClock() { resetImpl(); }

    void resetImpl();

    void setSpeedImpl(double speed);

    void updateImpl(double pts);

    double timeImpl() const;

    double delayImpl(double nextPts) const;

private:
    static double nowSec();

private:
    std::atomic<double> clockPts_{0.0};
    std::atomic<double> speed_{1.0};
    std::atomic<double> baseSysTime_{0.0};
};


#endif // VIDEOCLOCK_H
