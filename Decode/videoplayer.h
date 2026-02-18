#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include "demuxer.h"
#include "videoclock.h"
#include "videodecoder.h"
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
    void seek(double seconds);

    // 非阻塞，UI / render thread 用
    bool peekVideoFrame(VideoFrame*& frame);
    void popVideoFrame();

    VideoClock& clock() { return videoClock; }


private:
    void demuxLoop();
    void videoDecodeLoop();

    void renderLoop();

private:
    std::string url;

    Demuxer demux;
    VideoDecoder videoDec;
    VideoClock videoClock;

    PacketQueue videoPktQueue;
    FrameQueue  videoFrameQueue;

    std::thread demuxThread;
    std::thread videoThread;

    std::atomic<bool> abort_{false};
};


#endif // VIDEOPLAYER_H
