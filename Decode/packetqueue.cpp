#include "packetqueue.h"

PacketQueue::PacketQueue(size_t maxPackets, size_t maxBytes) noexcept
    : max_packets_(maxPackets), max_bytes_(maxBytes)
{

}

PacketQueue::~PacketQueue() noexcept {
    close();
    flush();
}

PutResult PacketQueue::put(PacketData&& data, bool block) noexcept {
    std::unique_lock lock(mutex_);
    if (!data.isFlush && data.pkt) {
        if (block) {
            cond_.wait(lock, [&]() {
                return closed_ ||
                       (queue_.size() < max_packets_ &&
                        total_size_ + data.pkt->size < max_bytes_);
            });
        }

        if (closed_) return PacketQueueClosed{};

        if (queue_.size() >= max_packets_ ||
            total_size_ + data.pkt->size >= max_bytes_) {
            return PacketQueueFull{};
        }

        total_size_ += data.pkt->size;
    }

    queue_.push_back(std::move(data));
    ++size_;

    cond_.notify_all(); // 唤醒 get / put
    return std::monostate{};
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
                   ? GetResult{PacketQueueClosed{}}
                   : GetResult{PacketQueueEmpty{}};
    }

    PacketData data{ std::move(queue_.front()) };

    // 视频播放完成时奔溃  data.pkt 为 null
    if (!data.isFlush && data.pkt)
        total_size_ -= data.pkt->size;
    queue_.pop_front();
    --size_;
    cond_.notify_all();
    return GetResult{std::move(data)};
}

// 清空队列
void PacketQueue::flush() noexcept {
    std::lock_guard lock(mutex_);
    queue_.clear();
    size_ = 0;
    total_size_ = 0;
    ++serial_;          // 参考 ffplay
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
