#include "audiodecoder.h"


bool AudioDecoder::open(const AVStream* stream)
{
    if (!stream || !stream->codecpar)
        return false;

    close();
    closed_ = false;

    streamIndex_ = stream->index;
    timeBase_    = stream->time_base;

    const AVCodec* codec =
        avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
        return false;

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_)
        return false;

    if (avcodec_parameters_to_context(codecCtx_,
                                      stream->codecpar) < 0)
        return false;

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0)
        return false;

    frame_ = av_frame_alloc();
    if (!frame_)
        return false;

    // -------- 获取源音频信息（FFmpeg 5+） --------

    AVChannelLayout srcLayout = codecCtx_->ch_layout;
    AVSampleFormat  srcFmt    = codecCtx_->sample_fmt;
    int             srcRate   = codecCtx_->sample_rate;

    // -------- 目标参数 --------

    dstChannels_ = 2;
    av_channel_layout_default(&codecCtx_->ch_layout, dstChannels_);

    // -------- 初始化 swr --------

    swrCtx_ = swr_alloc();

    av_opt_set_chlayout(swrCtx_, "in_chlayout",  &srcLayout, 0);
    av_opt_set_chlayout(swrCtx_, "out_chlayout", &codecCtx_->ch_layout, 0);

    av_opt_set_int(swrCtx_, "in_sample_rate",  srcRate, 0);
    av_opt_set_int(swrCtx_, "out_sample_rate", dstSampleRate_, 0);

    av_opt_set_sample_fmt(swrCtx_, "in_sample_fmt",  srcFmt, 0);
    av_opt_set_sample_fmt(swrCtx_, "out_sample_fmt", dstSampleFmt_, 0);

    if (swr_init(swrCtx_) < 0)
        return false;

    return true;
}


DecodeResult AudioDecoder::send(const PacketData& pkt)
{
    if (closed_)
        return DecodeResult::Closed;

    int ret;

    if (!pkt.pkt || !pkt.pkt->data)
        ret = avcodec_send_packet(codecCtx_, nullptr);
    else
        ret = avcodec_send_packet(codecCtx_, pkt.pkt.get());

    if (ret == AVERROR(EAGAIN))
        return DecodeResult::TryAgain;

    if (ret < 0)
        return DecodeResult::CodecError;

    return DecodeResult::Ok;
}

DecodeResult AudioDecoder::receive(AudioFrame& out)
{
    if (closed_)
        return DecodeResult::Closed;

    int ret = avcodec_receive_frame(codecCtx_, frame_);
    if (ret == AVERROR(EAGAIN))
        return DecodeResult::TryAgain;
    if (ret == AVERROR_EOF)
        return DecodeResult::Drained;
    if (ret < 0)
        return DecodeResult::CodecError;

    // ---------- 计算重采样后的样本数 ----------

    int64_t delay = swr_get_delay(swrCtx_, codecCtx_->sample_rate);

    int dstNbSamples = av_rescale_rnd(
        delay + frame_->nb_samples,
        dstSampleRate_,
        codecCtx_->sample_rate,
        AV_ROUND_UP
        );

    // ---------- 分配输出 buffer ----------

    out.data.resize(dstNbSamples * dstChannels_);

    uint8_t* outData[1] = {
        reinterpret_cast<uint8_t*>(out.data.data())
    };

    int converted = swr_convert(
        swrCtx_,
        outData,
        dstNbSamples,
        (const uint8_t**)frame_->data,
        frame_->nb_samples
        );

    if (converted < 0)
        return DecodeResult::CodecError;

    out.nbSamples  = converted;
    out.channels   = dstChannels_;
    out.sampleRate = dstSampleRate_;

    // ---------- 计算 pts ----------

    if (frame_->best_effort_timestamp != AV_NOPTS_VALUE)
    {
        out.pts = frame_->best_effort_timestamp *
                  av_q2d(timeBase_);
    }
    else
    {
        out.pts = 0.0;
    }

    av_frame_unref(frame_);

    return DecodeResult::FrameReady;
}
