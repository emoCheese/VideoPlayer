#ifndef AVFRAMEHOLDER_H
#define AVFRAMEHOLDER_H


extern "C" {
#include <libavutil/frame.h>
}

class AVFrameHolder {
public:
    AVFrameHolder() = default;

    explicit AVFrameHolder(AVFrame* f)
        : frame_(f) {}

    ~AVFrameHolder() {
        reset();
    }

    AVFrameHolder(AVFrameHolder&& other) noexcept {
        frame_ = other.frame_;
        other.frame_ = nullptr;
    }

    AVFrameHolder& operator=(AVFrameHolder&& other) noexcept {
        if (this != &other) {
            reset();
            frame_ = other.frame_;
            other.frame_ = nullptr;
        }
        return *this;
    }

    AVFrameHolder(const AVFrameHolder&) = delete;
    AVFrameHolder& operator=(const AVFrameHolder&) = delete;

    AVFrame* get() const { return frame_; }

    AVFrame* release() {
        AVFrame* tmp = frame_;
        frame_ = nullptr;
        return tmp;
    }

    void reset(AVFrame* f = nullptr) {
        if (frame_) {
            av_frame_free(&frame_);
        }
        frame_ = f;
    }

private:
    AVFrame* frame_ = nullptr;
};

#endif // AVFRAMEHOLDER_H
