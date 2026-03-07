#include "packetqueue.h"
#include <spdlog/spdlog.h>

PutStatus PacketQueue::put(PacketData &&data, bool block) noexcept {
    std::unique_lock lock(mutex_);

    if (!data.isFlush && data.pkt) {

        if (block) {
            not_full_.wait(lock, [&] {
                return closed_ ||
                       (queue_.size() < max_packets_ &&
                        total_bytes_ + data.pkt->size < max_bytes_);
            });
        }

        if (closed_)
            return PutStatus::Closed;

        if (queue_.size() >= max_packets_ ||
            total_bytes_ + data.pkt->size >= max_bytes_)
            return PutStatus::Full;

        total_bytes_ += data.pkt->size;
    }

    queue_.push_back(std::move(data));  // push_back 已有对象，emplace_back 临时对象
    not_empty_.notify_one();
    return PutStatus::Ok;
}

GetStatus PacketQueue::get(PacketData &out, bool block) noexcept {
    std::unique_lock lock(mutex_);

    if (block) {
        not_empty_.wait(lock, [&] {
            return closed_ || !queue_.empty();
        });
    }

    if (queue_.empty())
        return closed_ ? GetStatus::Closed
                       : GetStatus::Empty;

    out = std::move(queue_.front());

    if (!out.isFlush && out.pkt)
        total_bytes_ -= out.pkt->size;

    queue_.pop_front();
    not_full_.notify_one();
    return GetStatus::Ok;
}

void PacketQueue::flush() noexcept {
    std::lock_guard lock(mutex_);

    queue_.clear();
    total_bytes_ = 0;
    ++serial_;

    not_full_.notify_all();
}

void PacketQueue::close() noexcept {
    SPDLOG_INFO("PacketQueue Close");
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
}

void PacketQueue::start() noexcept {
    std::lock_guard lock(mutex_);
    closed_ = false;
    ++serial_;
}
