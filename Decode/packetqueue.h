#ifndef PACKETQUEUE_H
#define PACKETQUEUE_H

#include <variant>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#include <deque>
#include <mutex>
#include <condition_variable>
#include <cassert>

static constexpr unsigned int MAX_PACKETS = 100;
static constexpr unsigned int MAX_BYTES   = 15 * 1024 * 1024; // 15MB


struct PacketDeleter {
    void operator()(AVPacket* pkt) const noexcept {
        av_packet_free(&pkt);
    }
};

using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

inline PacketPtr make_packet() {
    return PacketPtr(av_packet_alloc());
}

struct PacketData {
    PacketPtr pkt;
    int serial;
    bool isFlush = false;   // 当flush时需要主动设置
};

enum class PutStatus {
    Ok,
    Full,
    Closed
};

enum class GetStatus {
    Ok,
    Empty,
    Closed
};

class PacketQueue {
public:
    PacketQueue(size_t maxPackets = MAX_PACKETS,
                size_t maxBytes   = MAX_BYTES) noexcept
        : max_packets_(maxPackets),
        max_bytes_(maxBytes) {}

    ~PacketQueue() noexcept = default;

    PutStatus put(PacketData&& data, bool block = true) noexcept;

    GetStatus get(PacketData& out, bool block = true) noexcept;

    void flush() noexcept;

    void close() noexcept;

    void start() noexcept;

    int serial() const noexcept { return serial_; }

private:
    std::deque<PacketData> queue_;
    size_t max_packets_;
    size_t max_bytes_;

    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;

    bool closed_ = false;
    size_t total_bytes_ = 0;
    int serial_ = 0;
};

#endif // PACKETQUEUE_H
