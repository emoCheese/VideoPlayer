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

    nv12Buffer = static_cast<uint8_t*>(
        av_malloc(width * height * 3 / 2)
        );
    if (!nv12Buffer)
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
    if (nv12Buffer) {
        av_free(nv12Buffer);
        nv12Buffer = nullptr;
    }
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
