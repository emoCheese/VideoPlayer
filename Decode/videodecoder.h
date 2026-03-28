#pragma once

#include <atomic>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}
#include "packetqueue.h"
#include "framequeue.h"
#include "ThreadSafeQueue.h"
#include "Command.h"
#include "Event.h"

enum class DecodeResult {
    Ok,            // send 成功（packet accepted）
    TryAgain,      // EAGAIN
    FrameReady,    // receive 成功输出一帧
    Drained,       // receive 返回 EOF
    Closed,
    CodecError,
    FatalError
};

class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();

    bool open(const AVStream* stream);
    void close();

    DecodeResult send(const PacketData& pkt);
    DecodeResult receive(VideoFrame& out);

    AVRational timeBase() const { return timeBase_; }
    int streamIndex() const { return streamIndex_; }

    // double getPtsSec() const;

    // 队列依赖注入
    using CommandQueue = SPSCQueue<Command>;
    using EventQueue = MPSCQueue<Event>;
    void setCommandQueue(CommandQueue* q);
    void setEventQueue(EventQueue* q);
    void setInputQueue(PacketQueue* q);
    void setOutputQueue(FrameQueue<VideoFrame>* q);

    // 线程控制
    void start();
    void stop();

private:
    AVCodecContext* codecCtx = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* swsCtx = nullptr;

    int width = 0;
    int height = 0;
    AVPixelFormat srcPixFmt = AV_PIX_FMT_NONE;

    AVRational timeBase_{};
    int streamIndex_ = -1;

    // 队列指针（依赖注入）
    CommandQueue* cmdQ_ = nullptr;
    EventQueue* eventQ_ = nullptr;
    PacketQueue* inputQ_ = nullptr;
    FrameQueue<VideoFrame>* outputQ_ = nullptr;

    // 线程控制
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int> currentSerial_{0};

    // 内部方法
    void run();
    void handleCommand(const Command& cmd);

    std::atomic<bool> closed_{false};
};

