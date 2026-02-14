#ifndef PLAYER_H
#define PLAYER_H

#include "ffmpeg.h"
#include <thread>

class Player
{
public:
    Player();

private:
    void dumexLoop();
    void videoDecodeLoop();

private:
    FFmpeg ffmpeg;

    std::thread dumexThread;
    std::thread videoThread;
    // to do audio thread
};

#endif // PLAYER_H
