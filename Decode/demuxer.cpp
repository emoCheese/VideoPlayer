#include "demuxer.h"
#include <spdlog/spdlog.h>

Demuxer::Demuxer() {}

Demuxer::~Demuxer() {
    stop();
    close();
}

bool Demuxer::open(std::string_view url) {
    url_ = url;

    if (avformat_open_input(&fmt_, url_.c_str(), nullptr, nullptr) < 0)
        return false;

    if (avformat_find_stream_info(fmt_, nullptr) < 0)
        return false;

    pkt_ = av_packet_alloc();
    return pkt_ != nullptr;
}

void Demuxer::close() {
    if (pkt_) {
        av_packet_free(&pkt_);
        pkt_ = nullptr;
    }
    if (fmt_) {
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
    }
}

void Demuxer::setPktQueue(PacketQueue* vq, PacketQueue* aq) {
    videoQ_ = vq;
    audioQ_ = aq;
}

void Demuxer::setCommandQueue(CommandQueue* q) {
    cmdQ_ = q;
}

void Demuxer::setEventQueue(EventQueue* q) {
    eventQ_ = q;
}

void Demuxer::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&Demuxer::run, this);
}

void Demuxer::stop() {
    if (!running_.exchange(false)) return;

    if (thread_.joinable())
        thread_.join();
}

void Demuxer::handleCommand(const Command& cmd) {
    std::visit([&](auto&& c) {
        using T = std::decay_t<decltype(c)>;

        if constexpr (std::is_same_v<T, CmdSeek>) {
            int64_t ts = static_cast<int64_t>(c.seconds * AV_TIME_BASE);
            av_seek_frame(fmt_, -1, ts, AVSEEK_FLAG_BACKWARD);

            videoQ_->start(); // serial++
            audioQ_->start();

            if (eventQ_)
                eventQ_->push(DecoderDrained{c.serial});
        }

        else if constexpr (std::is_same_v<T, CmdStop>) {
            running_ = false;
        }

    }, cmd);
}

bool Demuxer::readFrame(PacketData& out) {
    int ret = av_read_frame(fmt_, pkt_);
    if (ret < 0) return false;

    PacketPtr p = make_packet();
    av_packet_move_ref(p.get(), pkt_);

    out.pkt = std::move(p);
    out.streamIndex = out.pkt->stream_index;
    out.isFlush = false;

    av_packet_unref(pkt_);
    return true;
}

void Demuxer::run() {
    SPDLOG_INFO("Demux thread start");

    while (running_) {

        // 1️⃣ 先处理命令（关键）
        while (cmdQ_) {
            auto cmd = cmdQ_->try_pop();
            if (!cmd) break;
            handleCommand(*cmd);
        }

        // 2️⃣ 读数据
        PacketData data;
        if (!readFrame(data)) {
            if (eventQ_)
                eventQ_->push(DemuxerEOF{});
            break;
        }

        if (!data.pkt) continue;

        // 3️⃣ 分发
        if (videoQ_ && data.streamIndex == videoQ_->serial()) {
            data.serial = videoQ_->serial();
            videoQ_->put(std::move(data), true);
        } else if (audioQ_) {
            data.serial = audioQ_->serial();
            audioQ_->put(std::move(data), true);
        }
    }

    // 收尾
    if (videoQ_) videoQ_->close();
    if (audioQ_) audioQ_->close();

    SPDLOG_INFO("Demux thread exit");
}
