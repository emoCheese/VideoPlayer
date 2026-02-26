#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include "videodecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h> // FFmpeg 5.0+
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
}


struct AudioFrame {
    std::vector<float> data;   // interleaved
    int sampleRate = 0;
    int channels   = 0;
    int nbSamples  = 0;

    double pts = 0.0;
    int serial = 0;
};


class AudioDecoder {
public:
    AudioDecoder() = default;
    ~AudioDecoder();

    bool open(const AVStream *stream);
    void close();

    DecodeResult send(const PacketData &pkt);
    DecodeResult receive(AudioFrame &out);

    AVRational timeBase() const { return timeBase_; }
    int streamIndex() const { return streamIndex_; }
    double getPtsSec() const;

private:
    AVCodecContext *codecCtx_ = nullptr;
    AVFrame *frame_ = nullptr;

    SwrContext *swrCtx_ = nullptr;
    AVRational timeBase_{};
    int streamIndex_ = -1;
    int dstSampleRate_ = 48000;

    AVSampleFormat dstSampleFmt_ = AV_SAMPLE_FMT_FLT;
    uint64_t dstChannelLayout_ = AV_CH_LAYOUT_STEREO;

    int dstChannels_ = 2;
    std::atomic<bool> closed_{false};
};
#endif // AUDIODECODER_H
