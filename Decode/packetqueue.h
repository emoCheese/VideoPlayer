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
};

struct PacketQueueClosed {};   // abort_request
struct PacketQueueEmpty {};    // 非阻塞但无数据
using GetResult = std::variant<PacketData, PacketQueueClosed, PacketQueueEmpty>;

struct PacketQueueFull {};

using PutResult = std::variant<
    std::monostate, // 成功
    PacketQueueFull,
    PacketQueueClosed
    >;

class PacketQueue {
public:
    PacketQueue(
        size_t maxPackets = MAX_PACKETS,
        size_t maxBytes = MAX_BYTES) noexcept;

    ~PacketQueue() noexcept;

    // put：接管 pkt 所有权 不创建 pkt
    PutResult put(PacketPtr pkt, bool block = true) noexcept;

    // block = true 等价 ffplay 的 block
    GetResult get(bool block = true) noexcept;

    // 立刻丢弃队列里还没被消费的数据
    void flush() noexcept;

    void close() noexcept;

    void start() noexcept;

    int serial() const noexcept { return serial_; }

private:
    struct Item {
        PacketPtr pkt;
        int serial;
    };

    std::deque<Item> queue_;
    size_t max_packets_;
    size_t max_bytes_;

    mutable std::mutex mutex_;
    std::condition_variable cond_;

    bool closed_ = false;
    int size_ = 0;
    int total_size_ = 0;
    int serial_ = 0;
};

#endif // PACKETQUEUE_H
