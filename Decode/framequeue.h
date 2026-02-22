#ifndef FRAMEQUEUE_H
#define FRAMEQUEUE_H

#include <condition_variable>
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
    std::vector<uint8_t> data;
    double pts = 0.0;
    int serial = 0;
};

enum class PushResult {
    Ok,
    Closed
};

/**
 * @brief The FrameQueue class
 * Frame 环形队列
 */
class FrameQueue {
public:
    explicit FrameQueue(size_t capacity);

    // 生产者：阻塞 push
    PushResult push(VideoFrame&& frame);

    // 消费者：阻塞 pop
    bool pop(VideoFrame& out);

    // 清空队列（例如 seek）
    void flush([[maybe_unused]] int newSerial);

    // 关闭队列
    void close();

    size_t size() const;

    bool empty() const;

    void notifyAll();

private:
    std::vector<VideoFrame> queue_;
    const size_t capacity_;

    size_t rindex_ = 0;
    size_t windex_ = 0;
    size_t size_   = 0;

    bool closed_ = false;

    mutable std::mutex mtx_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
};

#endif // FRAMEQUEUE_H
