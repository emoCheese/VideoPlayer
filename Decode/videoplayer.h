#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

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
    VideoPlayer(const std::string& u);
    ~VideoPlayer();

    void start();
    void stop();

    void pause();
    void play();

    void seek(double seconds);  // todo 待实现

    void startClock(std::function<void(std::shared_ptr<VideoFrame>)> cb) { masterClock.start(cb); }

    MasterClock& clock() { return masterClock; }

private:
    void demuxLoop();
    void videoDecodeLoop();

private:
    std::string url;

    Demuxer demux;
    VideoDecoder videoDec;
    MasterClock masterClock;

    PacketQueue videoPktQueue;
    FrameQueue<VideoFrame>  videoFrameQueue;
    // FrameQueue<AudioFrame>  audioFrameQueue;

    std::thread demuxThread;
    std::thread videoThread;

    std::atomic<bool> abort_{false};
};


#endif // VIDEOPLAYER_H
