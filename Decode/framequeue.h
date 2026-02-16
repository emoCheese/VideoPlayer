#ifndef FRAMEQUEUE_H
#define FRAMEQUEUE_H

#include <condition_variable>
#include <deque>
#include <mutex>
extern "C" {
#include <stdint.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>
}
#include <vector>
#include <variant>

struct VideoFrame {
    int width = 0;
    int height = 0;
    AVPixelFormat format = AV_PIX_FMT_NV12;
    std::vector<uint8_t> data;   // NV12
    int64_t pts = AV_NOPTS_VALUE;
    int serial = 0;
};

struct FrameQueueClosed {};
struct FrameQueueFull {};
struct FrameQueueEmpty {};

using FrameResult = std::variant<VideoFrame, FrameQueueFull, FrameQueueEmpty, FrameQueueClosed>;

class FrameQueue {
public:
    explicit FrameQueue(size_t maxSize);

    void close();
    void flush(int newSerial);

    FrameResult push(VideoFrame&& frame, bool block);
    FrameResult pop(bool block = true);

    size_t size() const;

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;

    std::deque<VideoFrame> queue_;
    size_t maxSize_;

    bool closed_ = false;
    int serial_ = 0;
};


#endif // FRAMEQUEUE_H
