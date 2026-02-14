#include "packetqueue.h"

PacketQueue::PacketQueue(size_t maxPackets, size_t maxBytes) noexcept
    : max_packets_(maxPackets), max_bytes_(maxBytes)
{

}

PacketQueue::~PacketQueue() noexcept {
    close();
    flush();
}

bool PacketQueue::put(PacketPtr pkt) noexcept {
    if (!pkt) return false;

    std::unique_lock<std::mutex> lock(mutex_);
    // 🔒 阻塞等待：对齐 ffplay 行为
    cond_.wait(lock, [&]() {
        if (closed_) return true;
        return queue_.size() < max_packets_ && total_size_ + pkt->size < max_bytes_;
    });
    if (closed_) return false;
    queue_.push_back({ std::move(pkt), serial_ });

    ++size_;
    total_size_ += queue_.back().pkt->size;
    cond_.notify_all(); // 唤醒 get / put
    return true;
}

GetResult PacketQueue::get(bool block) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);

    if (block) {
        cond_.wait(lock, [&] {
            return closed_ || !queue_.empty();
        });
    }

    if (queue_.empty()) {
        return closed_
                   ? GetResult{QueueClosed{}}
                   : GetResult{QueueEmpty{}};
    }

    PacketData data = {
        std::move(queue_.front().pkt),
        queue_.front().serial
    };

    total_size_ -= data.pkt->size;
    queue_.pop_front();
    --size_;
    cond_.notify_all();
    return GetResult{std::move(data)};
}

void PacketQueue::flush() noexcept {
    std::lock_guard lock(mutex_);
    queue_.clear();
    size_ = 0;
    total_size_ = 0;
    ++serial_;          // ⭐ 对齐 ffplay
     cond_.notify_all();
}

void PacketQueue::close() noexcept {
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }
    cond_.notify_all();
}

void PacketQueue::start() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = false;
    ++serial_;          // ⭐ 对齐 ffplay
    cond_.notify_all();
}
