#include "videodecoder.h"

VideoDecoder::~VideoDecoder() { close(); }

bool VideoDecoder::open(const AVStream* stream)
{
    if (!stream || !stream->codecpar)
        return false;
    close();
    closed_ = false;
    streamIndex_ = stream->index;
    timeBase_    = stream->time_base;

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
        return false;

    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx)
        return false;

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
    if (closed_ || !codecCtx) return DecodeResult::Error;
    int ret = -1;
    if (!pkt.pkt || pkt.pkt->data == nullptr)
        ret = avcodec_send_packet(codecCtx, nullptr);
    else
        ret = avcodec_send_packet(codecCtx, pkt.pkt.get());

    if (ret == AVERROR(EAGAIN))
        return DecodeResult::TryAgain;
    if (ret < 0)
        return DecodeResult::Error;
    return DecodeResult::FrameReady;
}

DecodeResult VideoDecoder::receive(VideoFrame &out)
{
    int ret = avcodec_receive_frame(codecCtx, frame);
    if (ret == AVERROR(EAGAIN))
        return DecodeResult::TryAgain;
    if (ret == AVERROR_EOF)
        return DecodeResult::Drained;
    if (ret < 0)
        return DecodeResult::Error;

    uint8_t* dst[2] = {
        nv12Buffer,
        nv12Buffer + width * height
    };
    int linesize[2] = { width, width };

    // 将 yuv 数据转换为 NV12
    sws_scale(
        swsCtx,
        frame->data,
        frame->linesize,
        0,
        height,
        dst,
        linesize
        );

    out.width  = width;
    out.height = height;
    out.format = AV_PIX_FMT_NV12;
    out.pts    = frame->best_effort_timestamp;
    out.data.assign(
        nv12Buffer,
        nv12Buffer + width * height * 3 / 2
        );

    av_frame_unref(frame);
    return DecodeResult::FrameReady;
}
