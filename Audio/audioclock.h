#ifndef AUDIOCLOCK_H
#define AUDIOCLOCK_H

#include "IClockSource.h"
#include <SDL3/SDL.h>
#include <mutex>

class AudioClock : public IClockSource
{
public:
    void attachStream(SDL_AudioStream* stream,
                      int sampleRate,
                      int channels)
    {
        stream_ = stream;
        sampleRate_ = sampleRate;
        channels_ = channels;
    }

    void set(double pts) override
    {
        std::lock_guard lock(mutex_);
        last_pts_ = pts;
    }

    double now() const override
    {
        std::lock_guard lock(mutex_);

        if (!stream_)
            return last_pts_;

        int queued = SDL_GetAudioStreamQueued(stream_);

        double queued_sec =
            queued /
            (double)(sizeof(float) * channels_ * sampleRate_);

        return last_pts_ - queued_sec;
    }

    void pause(bool p) override
    {
        paused_ = p;
    }

private:
    SDL_AudioStream* stream_ = nullptr;

    int sampleRate_ = 0;
    int channels_ = 0;

    double last_pts_ = 0;

    bool paused_ = false;

    mutable std::mutex mutex_;
};
#endif // AUDIOCLOCK_H
