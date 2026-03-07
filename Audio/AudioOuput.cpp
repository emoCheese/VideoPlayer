#include "AudioOuput.h"
#include "audioclock.h"
#include <SDL3/SDL_audio.h>

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
bool AudioOutput::open()
{
    // 这里填写默认参数，后续会通过设备获取到真实参数进行覆盖
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.freq     = 48000;
    spec.channels = 2;


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

    SDL_AudioSpec obtained{};
    SDL_GetAudioStreamFormat(stream_, nullptr, &obtained);

    sampleRate_ = obtained.freq;
    channels_   = obtained.channels;

    SPDLOG_INFO(
        "Audio device opened: freq={}Hz channels={}",
        sampleRate_,
        channels_
        );

    if (!SDL_ResumeAudioStreamDevice(stream_))
    {
        SPDLOG_ERROR("ResumeAudioStreamDevice failed: {}", SDL_GetError());
        return false;
    }

    dynamic_cast<AudioClock&>(clock_)
        .attachStream(stream_, sampleRate_, channels_);

    maxQueuedBytes_ =
        sampleRate_ * channels_ *
        sizeof(float) * 0.2; // 200ms

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

        if (!fifo_.pop(block))  // 没有考虑 fifo 关闭，可能先关闭fifo 最后关闭 AudioOutput
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
