#include "framequeue.h"
#include <spdlog/spdlog.h>

FrameQueue::FrameQueue(size_t capacity)
    : queue_(capacity),
    capacity_(capacity)
{
}

bool FrameQueue::push(VideoFrame&& frame)
{
    std::lock_guard<std::mutex> lock(mtx_);

    if (closed_)
        return false;

    if (size_ >= capacity_)
        return false; // 满了，拒绝

    queue_[windex_] = std::move(frame);
    windex_ = (windex_ + 1) % capacity_;
    size_++;

    return true;
}

// 渲染线程通常先 peek 获取帧进行渲染，渲染完成后才调用 pop 移除
bool FrameQueue::peek(VideoFrame*& frame)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (size_ == 0) return false;
    frame = &queue_[rindex_];
    return true;
}

bool FrameQueue::pop()
{
    std::lock_guard<std::mutex> lock(mtx_);

    if (size_ == 0)
        return false;

    rindex_ = (rindex_ + 1) % capacity_;
    size_--;

    return true;
}

void FrameQueue::flush(int newSerial)
{
    std::lock_guard<std::mutex> lock(mtx_);

    rindex_ = 0;
    windex_ = 0;
    size_   = 0;

    // serial 通常在上层用
    (void)newSerial;
}

void FrameQueue::close()
{
    std::lock_guard<std::mutex> lock(mtx_);
    closed_ = true;
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
