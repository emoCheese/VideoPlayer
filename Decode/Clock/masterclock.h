#ifndef MASTERCLOCK_H
#define MASTERCLOCK_H

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include "IClockSource.h"
#include "framequeue.h"

template<typename T>
class FrameQueue;

struct VideoFrame;
/**
 * @brief The MasterClock class
 * MasterClock 只负责：
 * 调度帧 + 查询时间
 */
class MasterClock
{
public:
    // 将 VideoFrame 传递给外部渲染
    using Callback = std::function<void(std::shared_ptr<VideoFrame>)>;
    using CallbackPtr = void(*)(std::shared_ptr<VideoFrame>, void* ctx);

    enum class SyncType {
        Audio,
        Video,
        External
    };

    MasterClock(FrameQueue<VideoFrame>& queue);

    void start(Callback cb);
    void start(CallbackPtr cb);
    void stop();
    void pause(bool p);

    void setSyncType(SyncType type);
    void setAudioClock(IClockSource* c);
    void setVideoClock(IClockSource* c);
    void setExternalClock(IClockSource* c);

private:
    void loop();
    IClockSource* getMasterClock() const;

private:
    FrameQueue<VideoFrame>& queue_;
    Callback callback_;
    CallbackPtr callback_ptr_;

    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::thread thread_;

    std::atomic<SyncType> sync_type_{SyncType::External};

    IClockSource* audio_clock_ {nullptr};
    IClockSource* video_clock_ {nullptr};
    IClockSource* external_clock_ {nullptr};

    int current_serial_ {-1};

    const double drop_threshold_ {-0.08};
    const double fine_wait_threshold_ {0.002};
};

#endif // MASTERCLOCK_H
