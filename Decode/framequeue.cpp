#include "framequeue.h"
#include <spdlog/spdlog.h>


FrameQueue::FrameQueue(size_t capacity)
    : queue_(capacity),
    capacity_(capacity) {}

PushResult FrameQueue::push(VideoFrame &&frame)
{
    std::unique_lock<std::mutex> lock(mtx_);

    // 等待直到有空间或队列关闭
    notFull_.wait(lock, [&] {
        return size_ < capacity_ || closed_;
    });

    if (closed_) {
        spdlog::debug("FrameQueue::push aborted (closed)");
        return PushResult::Closed;
    }

    queue_[windex_] = std::move(frame);
    windex_ = (windex_ + 1) % capacity_;
    ++size_;

    notEmpty_.notify_one();
    return PushResult::Ok;
}

bool FrameQueue::pop(VideoFrame &out)
{
    std::unique_lock<std::mutex> lock(mtx_);

    // 等待直到有数据或关闭
    notEmpty_.wait(lock, [&] {
        return size_ > 0 || closed_;
    });

    if (size_ == 0) {
        // 可能是 closed 且无数据
        spdlog::info("Pop Failed, maybe size == 0 or queue closed");
        return false;
    }

    out = std::move(queue_[rindex_]);
    rindex_ = (rindex_ + 1) % capacity_;
    --size_;

    notFull_.notify_one();
    return true;
}

void FrameQueue::flush(int newSerial)
{
    std::lock_guard<std::mutex> lock(mtx_);

    rindex_ = 0;
    windex_ = 0;
    size_ = 0;


    spdlog::debug("FrameQueue flushed");

    notFull_.notify_all();
}

void FrameQueue::close()
{
    std::lock_guard<std::mutex> lock(mtx_);
    closed_ = true;

    spdlog::debug("FrameQueue closed");

    notifyAll();
}

size_t FrameQueue::size() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return size_;
}

bool FrameQueue::empty() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return size_ == 0;
}

void FrameQueue::notifyAll()
{
    notFull_.notify_all();
    notEmpty_.notify_all();
}
