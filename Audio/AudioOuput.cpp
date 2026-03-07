#include "AudioOuput.h"
#include "audioclock.h"
#include <SDL3/SDL_audio.h>
#include <memory_resource>

AudioOutput::AudioOutput(FrameQueue<AudioBlock>& fifo, PacketQueue& audioPktQueue, IClockSource &clock)
    : fifo_(fifo)
    , audioPktQueue_(audioPktQueue)
    , clock_(clock)
{
    SDL_Init(SDL_INIT_AUDIO);
}

AudioOutput::~AudioOutput()
{
    stop();
    if (stream_)
    {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
/**
 * @brief AudioOutput::open
 * @param sampleRate 采样率
 * @param channels   通道数
 * 1.open device 2.create 3.stream bind stream
 */
bool AudioOutput::open(int sampleRate, int channels)
{
    SDL_AudioSpec spec{};
    spec.freq = sampleRate;
    spec.format = SDL_AUDIO_F32;
    spec.channels = channels;

    maxQueuedBytes_ =
        sampleRate * channels * sizeof(float) * 2; // 4s buffer

    stream_ = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &spec,
        nullptr,
        nullptr
        );

    if (!stream_)
    {
        SPDLOG_ERROR("OpenAudioDeviceStream failed: {}", SDL_GetError());
        return false;
    }

    if (!SDL_ResumeAudioStreamDevice(stream_))
    {
        SPDLOG_ERROR("ResumeAudioStreamDevice failed: {}", SDL_GetError());
        return false;
    }
    // 类型检查防止出错
    dynamic_cast<AudioClock&>(clock_)
        .attachStream(stream_, sampleRate, channels);

    sampleRate_ = sampleRate;
    channels_ = channels;

    return true;
}

void AudioOutput::start()
{
    if (running_)
        return;
    running_ = true;
    paused_ = false;
    thread_ = std::thread(&AudioOutput::threadFunc, this);
}

void AudioOutput::stop()
{
    running_ = false;

    if (thread_.joinable())
        thread_.join();
}



void AudioOutput::audioPause(bool p)
{
    paused_ = p;
    if (p)
        SDL_PauseAudioStreamDevice(stream_);
    else
        SDL_ResumeAudioStreamDevice(stream_);
}

void AudioOutput::seek(int serial)
{
    currentSerial_ = serial;
    SDL_ClearAudioStream(stream_);
}

void AudioOutput::threadFunc()
{
    SPDLOG_DEBUG("Auido Output thread started");
    AudioBlock block;
    const int targetQueued =
        sampleRate_ * channels_ *
        sizeof(float) * 0.2; // 200ms
    while (running_)
    {
        if (paused_)
        {
            SDL_Delay(10);
            continue;
        }

        int queued = SDL_GetAudioStreamQueued(stream_);

        if (queued >= targetQueued)
        {
            SDL_Delay(1);
            continue;
        }

        if (!fifo_.pop(block))
            continue;

        if (block.serial != audioPktQueue_.currentSerial())
            continue;

        SDL_PutAudioStreamData(
            stream_,
            block.data.data(),
            block.data.size() * sizeof(float)
            );

        clock_.set(block.pts);
    }
}
