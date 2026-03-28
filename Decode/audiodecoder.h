#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include "videodecoder.h"
#include "ThreadSafeQueue.h"
#include "Command.h"
#include "Event.h"
#include "packetqueue.h"
#include "framequeue.h"
#include <atomic>
#include <thread>

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


struct AudioBlock;



class AudioDecoder {
public:
    AudioDecoder() = default;
    ~AudioDecoder();

    bool open(const AVStream* stream, int deviceSampleRate, int deviceChannels);
    void close();

    DecodeResult send(const PacketData &pkt);
    DecodeResult receive(AudioBlock &out);

    // seek / flush
    void reset();

    // 倍速播放
    void setSpeed(double speed) { speed_ = speed; }

    AVRational timeBase() const { return timeBase_; }
    int streamIndex() const { return streamIndex_; }
    // double getPtsSec() const;

    // 队列依赖注入
    using CommandQueue = SPSCQueue<Command>;
    using EventQueue = MPSCQueue<Event>;
    void setCommandQueue(CommandQueue* q);
    void setEventQueue(EventQueue* q);
    void setInputQueue(PacketQueue* q);
    void setOutputQueue(FrameQueue<AudioBlock>* q);

    // 线程控制
    void start();
    void stop();

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
    double speed_ = 1.0;

    // 队列指针（依赖注入）
    CommandQueue* cmdQ_ = nullptr;
    EventQueue* eventQ_ = nullptr;
    PacketQueue* inputQ_ = nullptr;
    FrameQueue<AudioBlock>* outputQ_ = nullptr;

    // 线程控制
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int> currentSerial_{0};

    // 内部方法
    void run();
    void handleCommand(const Command& cmd);
};
#endif // AUDIODECODER_H
