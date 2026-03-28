#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include "AudioOuput.h"
#include "audiodecoder.h"
#include "demuxer.h"
#include "videodecoder.h"
#include "masterclock.h"
#include "ThreadSafeQueue.h"
#include "Event.h"
#include "Command.h"
#include "StateMachine.h"
#include <atomic>
#include <thread>
#include <memory>

// 原有的状态枚举保留，供内部使用（未来可移除）
enum class DecodeState {
    ReceiveFrames,   // 尽量从 decoder 掏帧
    NeedPacket,      // decoder 吃不出帧，需要 packet
    Draining,        // 已 send(nullptr)，等待 EOF
    Ended,
    Error
};

enum class DemuxState {
    Init,           // start / serial++
    Reading,        // 正常 av_read_frame
    Draining,       // EOF，送 flush packet
    Seeking,        // 预留：seek 中
    Ended,
    Error
};

class VideoPlayer {
public:
    using FrameCallback = void(*)(std::shared_ptr<VideoFrame>, void* ctx);

    VideoPlayer(const std::string& u);
    ~VideoPlayer();

    // 公共接口（UI 层调用）
    void start();
    void stop();
    void pause();
    void play();
    void seek(double seconds);  // 现在通过事件驱动实现

    inline int currentSerial() const noexcept { return audioPktQueue_.serial(); }

    void startExternalClock(std::function<void(std::shared_ptr<VideoFrame>)> cb);
    void startAudioClock(std::function<void(std::shared_ptr<VideoFrame>)> cb);
    void startClock(FrameCallback cb);

    MasterClock& clock() { return masterClock_; }

private:
    // 初始化时钟源（不变）
    void initClockSource();

    // 内部数据流处理函数（稍后修改为遵循“命令→数据→上报”顺序）
    void flushPackage();
    void demuxLoop();
    void audioDecodeLoop();
    void videoDecodeLoop();

    // 各模块的命令处理函数
    void handleDemuxCommand(const Command& cmd);
    void handleAudioDecCommand(const Command& cmd);
    void handleVideoDecCommand(const Command& cmd);
    void handleAudioRenderCommand(const Command& cmd);
    void handleVideoRenderCommand(const Command& cmd);

    // 上报事件辅助函数
    void reportEvent(Event&& e);

private:
    std::string url_;

    // 原有模块
    Demuxer demux_;
    VideoDecoder videoDec_;
    AudioDecoder audioDec_;
    MasterClock masterClock_;

    IClockSource* videoClock_ {nullptr};
    IClockSource* audioClock_ {nullptr};
    IClockSource* externalClock_ {nullptr};

    // 数据队列（保持不变）
    PacketQueue videoPktQueue_;
    PacketQueue audioPktQueue_;
    FrameQueue<VideoFrame> videoFrameQueue_;
    FrameQueue<AudioBlock> audioFifo_;

    AudioOutput audioOutput_;

    // 控制队列（新增）
    using EventQueue = MPSCQueue<Event>;
    using CommandQueue = SPSCQueue<Command>;
    EventQueue eventQueue_;
    CommandQueue demuxCmdQueue_;
    CommandQueue audioDecCmdQueue_;
    CommandQueue videoDecCmdQueue_;
    CommandQueue audioRenderCmdQueue_;
    CommandQueue videoRenderCmdQueue_;

    // 状态机（新增）
    std::unique_ptr<StateMachine> stateMachine_;

    // 线程
    std::thread demuxThread_;
    std::thread audioThread_;
    std::thread videoThread_;
    std::thread audioRenderThread_;  // AudioOutput 内部已有线程，此处仅标识

    std::atomic<bool> abort_{false};
};

#endif // VIDEOPLAYER_H
