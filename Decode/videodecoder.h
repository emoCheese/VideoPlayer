#pragma once

#include "packetqueue.h"
#include "FrameQueue.h"
#include "ThreadSafeQueue.h"
#include "Command.h"
#include "Event.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <thread>
#include <atomic>


enum class DecodeResult {
    FrameReady,
    FatalError,
    TryAgain,
    Drained,
    CodecError,
    Ok,
};

struct VideoFrame;

class VideoDecoder {
public:
    using CommandQueue = SPSCQueue<Command>;
    using EventQueue   = MPSCQueue<Event>;

    VideoDecoder();
    ~VideoDecoder();

    bool open(const AVStream* stream);
    void close();

    void start();
    void stop();

    void setPacketQueue(PacketQueue* q);
    void setFrameQueue(FrameQueue<VideoFrame>* fq);
    void setCommandQueue(CommandQueue* q);
    void setEventQueue(EventQueue* q);

private:
    void run();
    void handleCommand(const Command& cmd);

    DecodeResult send(const PacketData& pkt);
    DecodeResult receive(VideoFrame& frame);

    void reset();

private:
    AVCodecContext* codecCtx_ = nullptr;
    AVFrame* frame_ = nullptr;

    AVRational timeBase_{};
    int streamIndex_ = -1;

    PacketQueue* pktQ_ = nullptr;
    FrameQueue<VideoFrame>* frameQ_ = nullptr;

    CommandQueue* cmdQ_ = nullptr;
    EventQueue* eventQ_ = nullptr;

    std::thread thread_;
    std::atomic<bool> running_{false};

    int curSerial_ = -1;
};
