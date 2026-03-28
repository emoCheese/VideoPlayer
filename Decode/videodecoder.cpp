#include "videodecoder.h"
#include <spdlog/spdlog.h>

VideoDecoder::VideoDecoder() {}

VideoDecoder::~VideoDecoder() {
    stop();
    close();
}

bool VideoDecoder::open(const AVStream* stream)
{
    if (!stream || !stream->codecpar)
        return false;

    const AVCodec* codec =
        avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
        return false;

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_)
        return false;

    if (avcodec_parameters_to_context(codecCtx_, stream->codecpar) < 0)
        return false;

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0)
        return false;

    frame_ = av_frame_alloc();
    if (!frame_)
        return false;

    streamIndex_ = stream->index;
    timeBase_ = stream->time_base;

    return true;
}

void VideoDecoder::close()
{
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
}

void VideoDecoder::setPacketQueue(PacketQueue* q) { pktQ_ = q; }
void VideoDecoder::setFrameQueue(FrameQueue<VideoFrame>* fq) { frameQ_ = fq; }
void VideoDecoder::setCommandQueue(CommandQueue* q) { cmdQ_ = q; }
void VideoDecoder::setEventQueue(EventQueue* q) { eventQ_ = q; }

void VideoDecoder::start()
{
    if (running_.exchange(true)) return;
    thread_ = std::thread(&VideoDecoder::run, this);
}

void VideoDecoder::stop()
{
    if (!running_.exchange(false)) return;

    if (thread_.joinable())
        thread_.join();
}

void VideoDecoder::handleCommand(const Command& cmd)
{
    std::visit([&](auto&& c){
        using T = std::decay_t<decltype(c)>;

        if constexpr (std::is_same_v<T, CmdFlush>) {
            SPDLOG_DEBUG("VideoDecoder flush serial={}", c.serial);
            reset();
            curSerial_ = c.serial;
        }
        else if constexpr (std::is_same_v<T, CmdStop>) {
            running_ = false;
        }

    }, cmd);
}

DecodeResult VideoDecoder::send(const PacketData& pkt)
{
    int ret = (!pkt.pkt || !pkt.pkt->data)
    ? avcodec_send_packet(codecCtx_, nullptr)
    : avcodec_send_packet(codecCtx_, pkt.pkt.get());

    if (ret == AVERROR(EAGAIN)) return DecodeResult::TryAgain;
    if (ret < 0) return DecodeResult::CodecError;
    return DecodeResult::Ok;
}

DecodeResult VideoDecoder::receive(VideoFrame& out)
{
    int ret = avcodec_receive_frame(codecCtx_, frame_);

    if (ret == AVERROR(EAGAIN))
        return DecodeResult::TryAgain;
    if (ret == AVERROR_EOF)
        return DecodeResult::Drained;
    if (ret < 0)
        return DecodeResult::CodecError;

    out.frame = AVFrameHolder(frame_);
    out.width = frame_->width;
    out.height = frame_->height;
    out.format = (AVPixelFormat)frame_->format;

    if (frame_->best_effort_timestamp != AV_NOPTS_VALUE)
        out.pts = frame_->best_effort_timestamp * av_q2d(timeBase_);
    else
        out.pts = 0;

    av_frame_unref(frame_);
    return DecodeResult::FrameReady;
}

void VideoDecoder::reset()
{
    if (codecCtx_)
        avcodec_flush_buffers(codecCtx_);
}
void VideoDecoder::run()
{
    SPDLOG_INFO("VideoDecoder thread start");
    while (running_) {

        // 处理命令
        while (cmdQ_) {
            auto cmd = cmdQ_->try_pop();
            if (!cmd) break;
            handleCommand(*cmd);
        }

        // 取 packet
        PacketData pkt;
        auto status = pktQ_->get(pkt, true);

        if (status == GetStatus::Closed) {
            send(PacketData{}); // flush

            while (running_) {
                VideoFrame f;
                auto ret = receive(f);
                if (ret == DecodeResult::FrameReady) {
                    f.serial = curSerial_;
                    if (!frameQ_->push(std::move(f)))
                        break;
                } else if (ret == DecodeResult::Drained) {
                    break;
                } else if (ret == DecodeResult::TryAgain) {
                    continue;
                } else {
                    break;
                }
            }

            frameQ_->close();
            break;
        }

        if (status != GetStatus::Ok)
            continue;

        curSerial_ = pkt.serial;

        auto sret = send(pkt);
        if (sret == DecodeResult::FatalError)
            break;

        // 尽量吐帧
        while (running_) {
            VideoFrame f;
            auto ret = receive(f);

            if (ret == DecodeResult::FrameReady) {
                if (pkt.serial != pktQ_->serial())
                    continue;
                f.serial = pkt.serial;
                if (!frameQ_->push(std::move(f)))
                    break;
                continue;
            }

            if (ret == DecodeResult::TryAgain)
                break;

            if (ret == DecodeResult::Drained)
                break;

            if (ret == DecodeResult::FatalError)
                break;
        }
    }

    frameQ_->close();
    SPDLOG_INFO("VideoDecoder thread exit");
}
