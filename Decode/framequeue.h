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
#include <spdlog/spdlog.h>
#include "AVFrameHolder.h"

struct VideoFrame {
    int width = 0;
    int height = 0;
    AVPixelFormat format = AV_PIX_FMT_NONE;
    AVFrameHolder frame;   // RAII AVFrame
    double pts = 0.0;
    int serial = 0;
};


// struct AudioFrame {

// };
/**
 * @brief The FrameQueue class
 * Frame 环形队列
 */

template<typename T>
class FrameQueue {
public:
    using data_type = T;

    explicit FrameQueue(size_t capacity)
        : queue_(capacity),
        capacity_(capacity) {}

    // 阻塞 push
    bool push(T&& item)
    {
        std::unique_lock<std::mutex> lock(mtx_);

        notFull_.wait(lock, [&] {
            return size_ < capacity_ || closed_;
        });

        if (closed_)
            return false;

        queue_[windex_] = std::move(item);
        windex_ = (windex_ + 1) % capacity_;
        ++size_;

        notEmpty_.notify_one();
        return true;
    }

    // 非阻塞 push
    bool try_push(T&& item)
    {
        std::lock_guard<std::mutex> lock(mtx_);

        if (closed_ || size_ >= capacity_)
            return false;

        queue_[windex_] = std::move(item);
        windex_ = (windex_ + 1) % capacity_;
        ++size_;

        notEmpty_.notify_one();
        return true;
    }

    // 阻塞 pop
    // 语义：
    //  - true  : 成功取到数据
    //  - false : 队列已关闭且无数据
    bool pop(T& out)
    {
        std::unique_lock<std::mutex> lock(mtx_);

        notEmpty_.wait(lock, [&] {
            return size_ > 0 || closed_;
        });

        if (size_ == 0) {
            // 只能是 closed 且无数据
            return false;
        }

        out = std::move(queue_[rindex_]);
        rindex_ = (rindex_ + 1) % capacity_;
        --size_;

        notFull_.notify_one();
        return true;
    }

    // 非阻塞 pop
    bool try_pop(T& out)
    {
        std::lock_guard<std::mutex> lock(mtx_);

        if (size_ == 0)
            return false;

        out = std::move(queue_[rindex_]);
        rindex_ = (rindex_ + 1) % capacity_;
        --size_;

        notFull_.notify_one();
        return true;
    }

    // flush（用于 seek）
    // 语义：
    //  - 清空数据
    //  - 不改变 closed
    //  - 唤醒等待线程
    void flush()
    {
        std::lock_guard<std::mutex> lock(mtx_);

        rindex_ = 0;
        windex_ = 0;
        size_   = 0;

        SPDLOG_DEBUG("FrameQueue flushed");

        // 唤醒所有等待线程
        notFull_.notify_all();
        notEmpty_.notify_all();
    }

    // close
    void close()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        closed_ = true;

        SPDLOG_DEBUG("FrameQueue closed");

        notFull_.notify_all();
        notEmpty_.notify_all();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return size_;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return size_ == 0;
    }

    bool closed() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return closed_;
    }

private:
    std::vector<data_type> queue_;
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
