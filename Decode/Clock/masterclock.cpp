#include "masterclock.h"
#include "framequeue.h"
#include <chrono>
#include <spdlog/spdlog.h>


MasterClock::MasterClock(FrameQueue<VideoFrame>& queue)
    : queue_(queue)
{
}

void MasterClock::start(Callback cb)
{
    callback_ = cb;
    running_ = true;
    thread_ = std::thread(&MasterClock::loop, this);
}

void MasterClock::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

void MasterClock::pause(bool p)
{
    paused_ = p;

    if (audio_clock_) audio_clock_->pause(p);
    if (video_clock_) video_clock_->pause(p);
    if (external_clock_) external_clock_->pause(p);
    spdlog::debug("MasterClock::pause: {}", p);
}

void MasterClock::setSyncType(SyncType type)
{
    sync_type_ = type;
}

void MasterClock::setAudioClock(IClockSource* c)
{
    audio_clock_ = c;
}

void MasterClock::setVideoClock(IClockSource* c)
{
    video_clock_ = c;
}

void MasterClock::setExternalClock(IClockSource* c)
{
    external_clock_ = c;
}

IClockSource* MasterClock::getMasterClock() const
{
    switch (sync_type_.load()) {
    case SyncType::Audio: return audio_clock_;
    case SyncType::Video: return video_clock_;
    case SyncType::External: return external_clock_;
    }
    return nullptr;
}

void MasterClock::loop()
{
    using Clock = std::chrono::steady_clock;

    double playback_start_time = 0.0;
    bool first_frame = true;

    while (running_) {

        VideoFrame frame;
        if (!queue_.pop(frame))
            break;

        while (paused_ && running_)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));

        const int serial = frame.serial;

        if (serial != current_serial_) {
            current_serial_ = serial;
            first_frame = true;
        }

        const double pts = frame.pts;

        const double system_now =
            std::chrono::duration<double>(
                Clock::now().time_since_epoch()).count();

        // 初始化播放基准
        if (first_frame) {
            playback_start_time = system_now - pts;
            first_frame = false;

            spdlog::info("Playback start at {:.6f}", playback_start_time);
        }

        // 计算目标时间（绝对时间模型）
        double target_time = playback_start_time + pts;
        double delay = target_time - system_now;

        // 丢帧
        if (delay < drop_threshold_) {
            spdlog::debug("Drop frame pts={:.6f} delay={:.6f}", pts, delay);
            continue;
        }

        // Hybrid Sleep
        if (delay > 0) {

            // 粗睡眠
            if (delay > 0.002) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(
                        static_cast<int>((delay - 0.001) * 1e6)));
            }

            // 精细自旋
            while (running_) {
                double now =
                    std::chrono::duration<double>(
                        Clock::now().time_since_epoch()).count();

                if (target_time - now <= 0)
                    break;

                std::this_thread::yield();
            }
        }

        // 渲染
        if (callback_) {
            callback_(std::make_shared<VideoFrame>(std::move(frame)));
        }

        if (video_clock_)
            video_clock_->set(pts);
    }
}
