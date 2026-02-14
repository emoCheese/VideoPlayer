#include "framequeue.h"


FrameQueue::FrameQueue(size_t maxSize)
    : maxSize_(maxSize) {}

FrameResult FrameQueue::push(VideoFrame&& frame, bool block)
{
    std::unique_lock lock(mtx_);
    // 防止无限增长
    while (!closed_ && queue_.size() >= maxSize_) {
        if (!block)
            return FrameQueueFull{};
        cv_.wait(lock);
    }

    if (closed_)
        return FrameQueueClosed{};

    queue_.push_back(std::move(frame));
    cv_.notify_all();
    return queue_.back();
}


FrameResult FrameQueue::pop(bool block)
{
    std::unique_lock lock(mtx_);
    while (!closed_ && queue_.empty()) {
        if (!block)
            return FrameQueueEmpty{};
        cv_.wait(lock);
    }

    if (queue_.empty())
        return FrameQueueClosed{};

    VideoFrame frame = std::move(queue_.front());
    queue_.pop_front();
    cv_.notify_all();
    return frame;
}

void FrameQueue::flush(int newSerial)
{
    std::lock_guard lock(mtx_);
    queue_.clear();
    serial_ = newSerial;
    cv_.notify_all();
}

void FrameQueue::close()
{
    std::lock_guard lock(mtx_);
    closed_ = true;
    queue_.clear();
    cv_.notify_all();
}


size_t FrameQueue::size() const
{
    std::lock_guard lock(mtx_);
    return queue_.size();
}
