#pragma once

#include "framequeue.h"
#include <SDL3/SDL.h>
#include "IClockSource.h"
#include "packetqueue.h"

class AudioOutput
{
public:
    explicit AudioOutput(FrameQueue<AudioBlock>& fifo,
                         PacketQueue& audioPktQueue,
                         IClockSource& clock);
    ~AudioOutput();

    bool open(int sampleRate, int channels);

    void start();
    void stop();

    void audioPause(bool p);

    void setPlaybackRate(double rate);

    /**
     * @brief seek
     * @param serial 序列号
     * 语义
     * 1 flush AudioFifo
     * 2 清空 SDL buffer
     * 3 更新 serial
     */
    void seek(int serial);

    auto audioStream() const { return stream_; }


private:
    void threadFunc();

private:
    FrameQueue<AudioBlock>& fifo_;
    PacketQueue& audioPktQueue_;
    IClockSource& clock_;

    SDL_AudioStream* stream_ = nullptr;
    int maxQueuedBytes_ = 0;

    std::thread thread_;

    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};

    int sampleRate_{0};
    int channels_{0};

    int currentSerial_ = 0;
};
