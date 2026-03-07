#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include "AudioOuput.h"
#include "audiodecoder.h"
#include "demuxer.h"
#include "videodecoder.h"
#include "masterclock.h"
#include <atomic>
#include <thread>

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

    void start();
    void stop();

    void pause();
    void play();

    void seek(double seconds);  // todo 待实现

    inline int currentSerial() const noexcept { return audioPktQueue_.serial(); }

    void startExternalClock(std::function<void(std::shared_ptr<VideoFrame>)> cb);
    void startAudioClock(std::function<void(std::shared_ptr<VideoFrame>)> cb);
    void startClock(FrameCallback cb);

    MasterClock& clock() { return masterClock_; }

private:
    void initClockSource();

    void flushPackage();
    void demuxLoop();
    void audioDecodeLoop();
    void videoDecodeLoop();

private:
    std::string url_;

    Demuxer demux_;
    VideoDecoder videoDec_;
    AudioDecoder audioDec_;
    MasterClock masterClock_;

    IClockSource* videoClock_ {nullptr};    // 未实现
    IClockSource* audioClock_ {nullptr};
    IClockSource* externalClock_ {nullptr};

    PacketQueue videoPktQueue_;
    PacketQueue audioPktQueue_;
    FrameQueue<VideoFrame> videoFrameQueue_;
    FrameQueue<AudioBlock> audioFifo_;


    AudioOutput audioOutput_;

    std::thread demuxThread_;
    std::thread audioThread_;
    std::thread videoThread_;

    std::atomic<bool> abort_{false};
};


#endif // VIDEOPLAYER_H
