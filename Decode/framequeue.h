#ifndef FRAMEQUEUE_H
#define FRAMEQUEUE_H

extern "C" {
#include <stdint.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>
}
#include <mutex>
#include <vector>

struct VideoFrame {
    int width = 0;
    int height = 0;
    AVPixelFormat format = AV_PIX_FMT_NV12;
    std::vector<uint8_t> data;   // NV12
    int64_t pts = AV_NOPTS_VALUE;
    int serial = 0;
};

class FrameQueue {
public:
    explicit FrameQueue(size_t capacity);

    // 解码线程用
    bool push(VideoFrame&& frame);

    // 渲染线程用，先peek获取指针，渲染完成后再pop
    bool peek(VideoFrame*& frame);
    bool pop();

    // 控制
    void flush(int newSerial);
    void close();

    size_t size() const;
    bool empty() const;

private:
    std::vector<VideoFrame> queue_;
    const size_t capacity_;

    size_t rindex_ = 0; // read index
    size_t windex_ = 0; // write index
    size_t size_   = 0;

    bool closed_ = false;

    mutable std::mutex mtx_;
};

#endif // FRAMEQUEUE_H
