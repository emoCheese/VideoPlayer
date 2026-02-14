#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include "ffmpeg.h"
#include <atomic>
#include <thread>

class VideoPlayer {
public:
    VideoPlayer(const std::string& url);
    ~VideoPlayer();

    void start();
    void stop();
    void seek(double seconds);

private:
    void demuxLoop();
    void videoDecodeLoop();

private:
    FFmpeg ffmpeg;

    PacketQueue videoPktQueue;
    FrameQueue  videoFrameQueue;

    std::thread demuxThread;
    std::thread videoThread;

    std::atomic<bool> abort_{false};
};


#endif // VIDEOPLAYER_H
