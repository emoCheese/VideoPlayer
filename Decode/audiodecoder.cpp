#include "audiodecoder.h"


// 检查是否为新版 FFmpeg（5.0+）
// V_CHANNEL_ORDER_NATIVE 是在 FFmpeg 5.0 的 libavutil/channel_layout.h 中定义的枚举值，
// 因此可以作为新版 API 的标志。如果编译器找不到该宏，则自动进入旧版兼容分支
extern "C" {
#include <libavutil/version.h>
}
#if LIBAVUTIL_VERSION_MAJOR >= 57  // FFmpeg 5.0+ uses AVChannelLayout
#define FFMPEG_NEW_CHANNEL_LAYOUT 1
#else
#define FFMPEG_NEW_CHANNEL_LAYOUT 0
#endif
AudioDecoder::~AudioDecoder()
{
    close();
}

bool AudioDecoder::open(const AVStream* stream, int deviceSampleRate, int deviceChannels)
{
    if (!stream || !stream->codecpar)
        return false;

    close();
    closed_ = false;

    streamIndex_ = stream->index;
    timeBase_    = stream->time_base;

    // 查找解码器
    const AVCodec* codec =
        avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
        return false;

    // 分配上下文
    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_)
        return false;

    // 复制参数
    if (avcodec_parameters_to_context(codecCtx_,
                                      stream->codecpar) < 0)
        return false;

    // 打开解码器
    if (avcodec_open2(codecCtx_, codec, nullptr) < 0)
        return false;

    frame_ = av_frame_alloc();
    if (!frame_)
        return false;

    // -------- 获取源音频信息 --------
    int             srcRate   = codecCtx_->sample_rate;
    AVSampleFormat  srcFmt    = codecCtx_->sample_fmt;

    dstSampleRate_ = deviceSampleRate;

    // -------- 目标参数--------
    dstSampleRate_ = deviceSampleRate;          // 目标采样率
    dstSampleFmt_  = AV_SAMPLE_FMT_FLT;         // 目标格式 标准 PCM（float32 interleaved）
    dstSampleRate_ = deviceSampleRate;          // 目标声道数

    // -------- 初始化 swr --------
#if FFMPEG_NEW_CHANNEL_LAYOUT
    // 新版：使用 AVChannelLayout
    AVChannelLayout srcLayout = codecCtx_->ch_layout;
    AVChannelLayout dstLayout;
    av_channel_layout_default(&dstLayout, dstChannels_);

    swrCtx_ = swr_alloc();
    if (!swrCtx_) {
        SPDLOG_ERROR("Failed to allocate SwrContext");
        return false;
    }

    // 设置输入参数
    av_opt_set_chlayout(swrCtx_, "in_chlayout",  &srcLayout, 0);
    av_opt_set_int(swrCtx_, "in_sample_rate",    srcRate, 0);
    av_opt_set_sample_fmt(swrCtx_, "in_sample_fmt", srcFmt, 0);

    // 设置输出参数
    av_opt_set_chlayout(swrCtx_, "out_chlayout", &dstLayout, 0);
    av_opt_set_int(swrCtx_, "out_sample_rate",   dstSampleRate_, 0);
    av_opt_set_sample_fmt(swrCtx_, "out_sample_fmt", dstSampleFmt_, 0);

    if (swr_init(swrCtx_) < 0) {
        SPDLOG_ERROR("Failed to initialize SwrContext");
        return false;
    }
#else
    // 旧版：使用 uint64_t 表示布局
    // 获取源声道布局（优先使用 codecCtx->channel_layout，若为0则根据 channels 推导）
    uint64_t srcLayout = codecCtx_->channel_layout;
    if (srcLayout == 0) {
        srcLayout = av_get_default_channel_layout(codecCtx_->channels);
    }
    int srcChannels = codecCtx_->channels;

    // 目标声道布局
    uint64_t dstLayout = av_get_default_channel_layout(dstChannels_);
    if (dstLayout == 0) {
        // 如果无法推导（极少见），使用手动布局（例如立体声为 AV_CH_LAYOUT_STEREO）
        dstLayout = (dstChannels_ == 1) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    }

    swrCtx_ = swr_alloc_set_opts(nullptr,
                                 dstLayout, dstSampleFmt_, dstSampleRate_,
                                 srcLayout, srcFmt, srcRate,
                                 0, nullptr);
    if (!swrCtx_ || swr_init(swrCtx_) < 0) {
        SPDLOG_ERROR("Failed to create/resample SwrContext");
        return false;
    }
#endif

    return true;
}

void AudioDecoder::close()
{
    closed_ = true;
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (swrCtx_) {
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
    }
}



DecodeResult AudioDecoder::send(const PacketData& pkt)
{
    if (closed_) return DecodeResult::Closed;
    int ret = (!pkt.pkt || !pkt.pkt->data)
                  ? avcodec_send_packet(codecCtx_, nullptr)
                  : avcodec_send_packet(codecCtx_, pkt.pkt.get());

    if (ret == AVERROR(EAGAIN)) return DecodeResult::TryAgain;
    if (ret < 0) return DecodeResult::CodecError;
    return DecodeResult::Ok;
}

DecodeResult AudioDecoder::receive(AudioBlock &out)
{
    if (closed_) return DecodeResult::Closed;

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

    // ---------- 分配输出 buffer ----------
    out.data.resize(converted * dstChannels_);
    out.nbSamples  = converted;             // 存储实际转换得到的样本数（每个通道）
    out.channels   = dstChannels_;          // 输出音频的通道数
    out.sampleRate = dstSampleRate_;        // 输出音频的采样率
    // 输出音频的时长（秒），由样本数除以采样率计算得出
    out.duration = converted / (double)dstSampleRate_;

    SPDLOG_DEBUG(
        "samples={} duration={}",
        converted,
        converted / (double)dstSampleRate_
        );

    // ---------- 计算 pts ----------
    if (frame_->best_effort_timestamp != AV_NOPTS_VALUE)
    {
        out.pts = frame_->best_effort_timestamp *   // FFmpeg提供的最佳估计时间戳
                  av_q2d(timeBase_);    // 将时间基（有理数）转换为双精度浮点数，即每个时间单位的秒数
    }
    else
    {
        out.pts = 0.0;
    }
    av_frame_unref(frame_);
    return DecodeResult::FrameReady;
}

void AudioDecoder::reset()
{
    if (codecCtx_) {
        avcodec_flush_buffers(codecCtx_);
    }
}
