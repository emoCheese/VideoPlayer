#include "demuxer.h"
#include <spdlog/spdlog.h>

Demuxer::Demuxer() {}

Demuxer::~Demuxer() {
    stop();
    close();
}

bool Demuxer::open(std::string_view url) {
    url_ = url;

    close();  // 清理旧资源

    //  打开输入
    if (avformat_open_input(&fmt_, url_.c_str(), nullptr, nullptr) < 0) {
        SPDLOG_ERROR("avformat_open_input failed");
        return false;
    }

    // 读取流信息
    if (avformat_find_stream_info(fmt_, nullptr) < 0) {
        SPDLOG_ERROR("avformat_find_stream_info failed");
        return false;
    }

    // 查找音视频流
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;

    for (unsigned int i = 0; i < fmt_->nb_streams; ++i) {
        auto* codecpar = fmt_->streams[i]->codecpar;

        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_videoStreamIndex < 0) {
            m_videoStreamIndex = i;
        }
        else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO && m_audioStreamIndex < 0) {
            m_audioStreamIndex = i;
        }
    }

    if (m_videoStreamIndex < 0 && m_audioStreamIndex < 0) {
        SPDLOG_ERROR("No audio/video stream found");
        return false;
    }

    // 4️⃣ 分配 packet
    pkt_ = av_packet_alloc();
    if (!pkt_) {
        SPDLOG_ERROR("av_packet_alloc failed");
        return false;
    }

    SPDLOG_INFO("Open success: videoStream={}, audioStream={}",
                m_videoStreamIndex, m_audioStreamIndex);

    return true;
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

            // 刷新队列，插入flush包
            if (videoQ_) videoQ_->flush();
            if (audioQ_) audioQ_->flush();

            // 注意：不在此上报DecoderDrained，由decoder在drain后上报
        }
        else if constexpr (std::is_same_v<T, CmdPause>) {
            paused_ = true;
        }
        else if constexpr (std::is_same_v<T, CmdResume>) {
            paused_ = false;
        }
        else if constexpr (std::is_same_v<T, CmdStop>) {
            running_ = false;
            // 关闭队列以唤醒阻塞的消费者
            if (videoQ_) videoQ_->close();
            if (audioQ_) audioQ_->close();
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

        // 如果暂停，等待一段时间后继续
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // 2️⃣ 读数据
        PacketData data;
        if (!readFrame(data)) {
            if (eventQ_)
                eventQ_->push(DemuxerEOF{});
            break;
        }

        if (!data.pkt) continue;

        // 分发：根据流索引决定放入视频队列还是音频队列
        if (videoQ_ && data.streamIndex == getVideoStreamIndex()) {
            data.serial = videoQ_->serial();
            videoQ_->put(std::move(data), true);
        } else if (audioQ_ && data.streamIndex == getAudioStreamIndex()) {
            data.serial = audioQ_->serial();
            audioQ_->put(std::move(data), true);
        }
        // 忽略其他流（如字幕）
    }

    // 收尾
    if (videoQ_) videoQ_->close();
    if (audioQ_) audioQ_->close();

    SPDLOG_INFO("Demux thread exit");
}
