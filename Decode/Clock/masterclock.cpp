#include "masterclock.h"
#include <chrono>
#include <spdlog/spdlog.h>


MasterClock::MasterClock(FrameQueue<VideoFrame>& queue)
    : queue_(queue)
{
}

MasterClock::~MasterClock()
{
    stop();
}

void MasterClock::start(Callback cb)
{
    callback_ = cb;
    running_ = true;
    thread_ = std::thread(&MasterClock::loop, this);
}

void MasterClock::start(CallbackPtr cb)
{
    callback_ptr_ = cb;
    running_ = true;
    thread_ = std::thread([this](){
        // loop2(callback_ptr_);
    });
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
    SPDLOG_DEBUG("MasterClock::pause: {}", p);
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


#if 0
void MasterClock::loop()
{
    const double max_frame_duration = 0.5;
    const double min_sync_threshold = 0.004;
    const double max_sync_threshold = 0.1;

    const double drift_correction = 0.1;   // 渐进修正强度

    double frame_timer = 0.0;
    double frame_duration = 0.04;

    double last_pts = 0.0;
    bool first_frame = true;

    while (running_) {

        VideoFrame frame;
        if (!queue_.pop(frame))
            break;

        while (paused_ && running_)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        IClockSource* master = getMasterClock();
        if (!master) {
            SPDLOG_ERROR("Master clock null");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        double pts = frame.pts;

        if (first_frame) {
            frame_timer = master->now();
            last_pts = pts;

            first_frame = false;
            SPDLOG_INFO("Frame timer initialized {:.6f}", frame_timer);
        }

        // 计算 frame duration
        double duration = pts - last_pts;

        if (duration > 0 && duration < max_frame_duration)
            frame_duration = duration;

        last_pts = pts;

        // 计算 sync threshold
        double sync_threshold =
            std::max(min_sync_threshold,
                     std::min(max_sync_threshold, frame_duration));

        // A/V drift
        double master_now = master->now();
        double diff = pts - master_now;

        // 计算 delay
        double delay = frame_duration;
        if (std::fabs(diff) < max_frame_duration) {
            if (diff <= -sync_threshold) {
                // video 落后
                delay = std::max(0.0, frame_duration + diff * drift_correction);
            }
            else if (diff >= sync_threshold) {
                // video 超前
                delay = frame_duration + diff * drift_correction;
            }
        }

        // 推进 timeline
        frame_timer += delay;

        // timeline reset
        if (fabs(frame_timer - master_now) > 0.5) {
            frame_timer = master_now;
        }

        // 计算等待时间
        double actual_delay = frame_timer - master_now;
        if (actual_delay < 0) {
            actual_delay = 0;
        }

        // Drop frame（严重落后）
        if (actual_delay < drop_threshold_) {
            SPDLOG_DEBUG(
                "Drop frame pts={:.6f} delay={:.6f}",
                pts, actual_delay);
            continue;
        }

        // Hybrid Sleep
        if (actual_delay > 0) {
            if (actual_delay > 0.002) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(
                        (int)((actual_delay - 0.001) * 1e6)));
            }
            while (running_) {
                if (frame_timer - master->now() <= 0)
                    break;
                std::this_thread::yield();
            }
        }

        // 渲染
        if (callback_) {
            callback_(std::make_shared<VideoFrame>(
                std::move(frame)));
        }

        // 更新 video clock
        if (sync_type_ == SyncType::Video && video_clock_) {
            video_clock_->set(pts);
        }

        SPDLOG_DEBUG(
            "delay={:.3f}s diff={:.3f} frame_dur={:.4f} queue={}",
            delay,
            diff,
            frame_duration,
            queue_.size());
    }
}
#else
// 通用 loop
void MasterClock::loop()
{
    double master_start_time = 0.0;
    bool first_frame = true;

    while (running_) {

        VideoFrame frame;
        if (!queue_.pop(frame)) {
            break;
        }

        while (paused_ && running_)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));

        IClockSource* master = getMasterClock();
        if (!master) {
            SPDLOG_ERROR("Master clock is null");
            continue;
        }

        const int serial = frame.serial;
        const double pts = frame.pts;

        // serial 切换重建锚点
        if (serial != current_serial_) {
            current_serial_ = serial;
            first_frame = true;
        }

        // 初始化绝对时间锚点
        if (first_frame) {
            master_start_time = master->now() - pts;
            first_frame = false;

            SPDLOG_INFO("Re-anchor master_start_time={:.6f}", master_start_time);
        }

        // 计算目标时间
        double master_now = master->now();
        double target_time = master_start_time + pts;
        double delay = target_time - master_now;

        auto diff = pts - master_now;
        SPDLOG_DEBUG(
            "delay={:.6f}, diff={:.6f}, queue={}",
            delay, diff, queue_.size());

        // 丢帧
        if (delay < drop_threshold_) {
            SPDLOG_DEBUG("Drop frame pts={:.6f}, delay={:.6f}", pts, delay);
            continue;
        }

        // Hybrid Sleep
        if (delay > 0) {
            if (delay > 0.002) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(
                        static_cast<int>((delay - 0.001) * 1e6)));
            }

            while (running_) {
                master_now = master->now();
                if (target_time - master_now <= 0)
                    break;
                std::this_thread::yield();
            }
        }

        // 渲染
        if (callback_) {
            callback_(std::make_shared<VideoFrame>(std::move(frame)));
        }

        // 如果是 Video Master，需要推进 video_clock
        if (sync_type_ == SyncType::Video && video_clock_) {
            video_clock_->set(pts);
        }
    }
}
#endif
