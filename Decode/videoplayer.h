#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include "demuxer.h"
#include "videoclock.h"
#include "videodecoder.h"
#include <atomic>
#include <thread>
#include "videorendergl.h"


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
    VideoPlayer(const std::string& u, const NativeWindow& win);
    ~VideoPlayer();

    void start();
    void stop();
    void seek(double seconds);

    FrameResult getVideoFrame(bool block = true);
private:
    void demuxLoop();
    void videoDecodeLoop();

    void renderLoop();

private:
    std::string url;

    Demuxer demux;
    VideoDecoder videoDec;
    VideoRenderGL renderGL;
    VideoClock videoClock;

    // window
    NativeWindow window_;

    PacketQueue videoPktQueue;
    FrameQueue  videoFrameQueue;

    std::thread demuxThread;
    std::thread videoThread;
    std::thread renderThread;

    std::atomic<bool> abort_{false};


};


#endif // VIDEOPLAYER_H
