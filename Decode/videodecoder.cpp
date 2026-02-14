#include "videodecoder.h"

VideoDecoder::VideoDecoder(AVCodecParameters *par)
    : params(par)
{}

VideoDecoder::~VideoDecoder()
{
    close();
}

bool VideoDecoder::openDecoder()
{
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec) {
        close();
        return false;
    }
    // 创建解码器上下文s
    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        close();
        return false;
    }
    // 从流参数复制配置
    if (avcodec_parameters_to_context(codecCtx, params) < 0) {
        close();
        return false;
    }

    // 打开解码器
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        close();
        return false;
    }

    // 初始化帧
    frame = av_frame_alloc();
    if (!frame) {
        close();
        return false;
    }

    // 获取帧尺寸
    width = codecCtx->width;
    height = codecCtx->height;

    // 创建转换上下文 (YUV420P -> NV12)
    swsCtx = sws_getContext(
        width, height,  codecCtx->pix_fmt, // AV_PIX_FMT_YUV420P
        width, height, AV_PIX_FMT_NV12,
        SWS_BILINEAR, nullptr, nullptr, nullptr
        );
    if (!swsCtx) {
        close();
        return false;
    }

    // 分配NV12缓冲区
    nv12Buffer = (uint8_t*)av_malloc(width * height * 3 / 2);
    if (!nv12Buffer) {
        close();
        return false;
    }
    return true;
}

void VideoDecoder::close()
{
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

    // sws + fill VideoFrame
    out.width  = width;
    out.height = height;
    out.format = AV_PIX_FMT_NV12;
    out.pts    = frame->best_effort_timestamp;

    uint8_t* dst[2] = {
        nv12Buffer,
        nv12Buffer + width * height
    };
    int linesize[2] = { width, width };

    sws_scale(
        swsCtx,
        frame->data,
        frame->linesize,
        0,
        height,
        dst,
        linesize
        );

    out.data.resize(width * height * 3 / 2);
    memcpy(out.data.data(), nv12Buffer, out.data.size());

    av_frame_unref(frame);
    return DecodeResult::FrameReady;
}
