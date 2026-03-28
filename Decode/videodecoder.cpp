#include "videodecoder.h"
#include <spdlog/spdlog.h>

VideoDecoder::~VideoDecoder() { close(); }

bool VideoDecoder::open(const AVStream* stream)
{
    if (!stream || !stream->codecpar)
        return false;

    close();
    closed_ = false;
    streamIndex_ = stream->index;
    timeBase_    = stream->time_base;

    SPDLOG_INFO("video time_base: {}/{}",
                stream->time_base.num,
                stream->time_base.den);

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
        return false;

    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx)
        return false;

    codecCtx->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;  // 确保输出所有帧
    if (avcodec_parameters_to_context(codecCtx, stream->codecpar) < 0)
        return false;

    if (avcodec_open2(codecCtx, codec, nullptr) < 0)
        return false;

    frame = av_frame_alloc();
    if (!frame)
        return false;

    width  = codecCtx->width;
    height = codecCtx->height;
    srcPixFmt = codecCtx->pix_fmt;

    swsCtx = sws_getContext(
        width, height, srcPixFmt,
        width, height, AV_PIX_FMT_NV12,
        SWS_BILINEAR,
        nullptr, nullptr, nullptr
        );
    if (!swsCtx)
        return false;

    return true;
}

void VideoDecoder::close()
{
    closed_ = true;
    if (codecCtx) {
        avcodec_free_context(&codecCtx);
        codecCtx = nullptr;
    }
    if (frame) {
        av_frame_free(&frame);
        frame = nullptr;
    }
    if (swsCtx) {
        sws_freeContext(swsCtx);
        swsCtx = nullptr;
    }
}

// ==================== 队列注入 ====================

void VideoDecoder::setCommandQueue(CommandQueue* q) {
    cmdQ_ = q;
}

void VideoDecoder::setEventQueue(EventQueue* q) {
    eventQ_ = q;
}

void VideoDecoder::setInputQueue(PacketQueue* q) {
    inputQ_ = q;
}

void VideoDecoder::setOutputQueue(FrameQueue<VideoFrame>* q) {
    outputQ_ = q;
}

// ==================== 线程控制 ====================

void VideoDecoder::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&VideoDecoder::run, this);
}

void VideoDecoder::stop() {
    if (!running_.exchange(false)) return;

    // 关闭队列以唤醒阻塞
    if (inputQ_) inputQ_->close();
    if (outputQ_) outputQ_->close();

    if (thread_.joinable())
        thread_.join();
}

// ==================== 命令处理 ====================

void VideoDecoder::handleCommand(const Command& cmd) {
    std::visit([&](auto&& c) {
        using T = std::decay_t<decltype(c)>;

        if constexpr (std::is_same_v<T, CmdFlush>) {
            const CmdFlush& cf = std::get<CmdFlush>(cmd);
            SPDLOG_DEBUG("VideoDecoder flush with serial {}", cf.serial);
            // 收到flush包时，会在run循环中处理，此处仅记录
        }
        else if constexpr (std::is_same_v<T, CmdPause>) {
            paused_ = true;
        }
        else if constexpr (std::is_same_v<T, CmdResume>) {
            paused_ = false;
        }
        else if constexpr (std::is_same_v<T, CmdStop>) {
            running_ = false;
            if (inputQ_) inputQ_->close();
            if (outputQ_) outputQ_->close();
        }
        // 其他命令忽略
    }, cmd);
}

// ==================== 线程主循环 ====================

void VideoDecoder::run() {
    SPDLOG_INFO("VideoDecoder thread start");

    while (running_) {
        // 1️⃣ 处理命令
        while (cmdQ_) {
            auto cmd = cmdQ_->try_pop();
            if (!cmd) break;
            handleCommand(*cmd);
        }

        // 2️⃣ 如果暂停，等待
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // 3️⃣ 从输入队列取包（非阻塞）
        PacketData pkt;
        GetStatus status = inputQ_ ? inputQ_->get(pkt, false) : GetStatus::Closed;
        if (status == GetStatus::Closed) {
            // 队列已关闭，退出循环
            break;
        }
        if (status == GetStatus::Empty) {
            // 无数据，短暂休眠后继续
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // 4️⃣ 处理 flush 包
        if (pkt.isFlush) {
            SPDLOG_DEBUG("VideoDecoder received flush packet, serial {}", pkt.serial);
            avcodec_flush_buffers(codecCtx);
            currentSerial_ = pkt.serial;
            continue;
        }

        // 5️⃣ 检查 serial 是否匹配
        if (pkt.serial != currentSerial_) {
            // 丢弃旧serial的包
            continue;
        }

        // 6️⃣ 发送给解码器
        DecodeResult sret = send(pkt);
        if (sret == DecodeResult::FatalError || sret == DecodeResult::Closed) {
            SPDLOG_ERROR("VideoDecoder send fatal error, stopping");
            if (eventQ_) eventQ_->push(DecoderError{});
            break;
        }

        // 7️⃣ 循环接收帧直到EAGAIN
        while (running_ && !paused_) {
            VideoFrame frame;
            DecodeResult rret = receive(frame);
            if (rret == DecodeResult::FrameReady) {
                frame.serial = currentSerial_;
                if (outputQ_ && !outputQ_->push(std::move(frame))) {
                    // 输出队列满，丢弃帧（或等待）
                    SPDLOG_WARN("VideoDecoder output queue full, dropping frame");
                }
                continue;
            } else if (rret == DecodeResult::TryAgain) {
                break; // 等待下一个包
            } else if (rret == DecodeResult::Drained) {
                // 解码器已排空，上报事件
                if (eventQ_) eventQ_->push(DecoderDrained{currentSerial_});
                break;
            } else if (rret == DecodeResult::FatalError || rret == DecodeResult::CodecError) {
                SPDLOG_ERROR("VideoDecoder receive error");
                if (eventQ_) eventQ_->push(DecoderError{});
                running_ = false;
                break;
            } else {
                // 其他情况（Closed）退出
                break;
            }
        }
    }

    // 收尾
    if (inputQ_) inputQ_->close();
    if (outputQ_) outputQ_->close();
    SPDLOG_INFO("VideoDecoder thread exit");
}

DecodeResult VideoDecoder::send(const PacketData &pkt)
{
    if (closed_) return DecodeResult::Closed;
    if (!codecCtx) return DecodeResult::FatalError;
    int ret = -1;
    if (!pkt.pkt || pkt.pkt->data == nullptr)   // flush
        ret = avcodec_send_packet(codecCtx, nullptr);
    else {
        // SPDLOG_INFO("Video packet size: {}, pts: {}, dts: {}",
        //             pkt.pkt->size, pkt.pkt->pts, pkt.pkt->dts);
        ret = avcodec_send_packet(codecCtx, pkt.pkt.get());
    }

    if (ret == AVERROR(EAGAIN))
        return DecodeResult::TryAgain;
    if (ret < 0)
        return DecodeResult::CodecError;
    return DecodeResult::Ok;
}

DecodeResult VideoDecoder::receive(VideoFrame &out)
{
    int ret = avcodec_receive_frame(codecCtx, frame);
    if (ret == AVERROR(EAGAIN))
        return DecodeResult::TryAgain;
    if (ret == AVERROR_EOF)
        return DecodeResult::Drained;
    if (ret < 0)
        return DecodeResult::CodecError;

    // 1️ 创建 NV12 输出 frame
    AVFrame* dst = av_frame_alloc();
    if (!dst)
        return DecodeResult::FatalError;

    dst->format = AV_PIX_FMT_NV12;
    dst->width  = width;
    dst->height = height;

    if (av_frame_get_buffer(dst, 32) < 0)
    {
        av_frame_free(&dst);
        return DecodeResult::FatalError;
    }

    // 2️ sws 直接写入 dst 的 buffer
    sws_scale(
        swsCtx,
        frame->data,
        frame->linesize,
        0,
        height,
        dst->data,
        dst->linesize
        );

    // 3️ 复制时间戳
    dst->pts = frame->pts;
    dst->best_effort_timestamp = frame->best_effort_timestamp;

    // 4️ 输出
    out.width  = width;
    out.height = height;
    out.format = AV_PIX_FMT_NV12;

    if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
        out.pts = frame->best_effort_timestamp * av_q2d(timeBase_);
    else
        out.pts = 0.0;

    out.frame.reset(dst);

    av_frame_unref(frame);

    return DecodeResult::FrameReady;
}
