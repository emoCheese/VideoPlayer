#include "videoplayer.h"
#include <iostream>
#include <qdebug.h>
#include <spdlog/spdlog.h>
#include "audioclock.h"
#include "externalclock.h"
#include "videoclock.h"
#include "ThreadSafeQueue.h"
#include "Event.h"
#include "Command.h"
#include "StateMachine.h"
#include <cassert>

VideoPlayer::VideoPlayer(const std::string &u)
    : url_(u)
    , videoPktQueue_(100, 16 * 1024 * 1024)
    , audioPktQueue_(300, 48 * 1024 * 1024)
    , masterClock_(videoFrameQueue_)
    , audioClock_(new AudioClock)
    , externalClock_(new ExternalClock)
    , videoFrameQueue_(8)
    , audioFifo_(24)
    , audioOutput_(audioFifo_, audioPktQueue_, *audioClock_)
    , eventQueue_(1024)                     // 事件队列容量
    , demuxCmdQueue_(64)
    , audioDecCmdQueue_(64)
    , videoDecCmdQueue_(64)
    , audioRenderCmdQueue_(64)
    , videoRenderCmdQueue_(64)
{
    // 创建状态机，传入队列引用
    stateMachine_ = std::make_unique<StateMachine>(
        eventQueue_,
        demuxCmdQueue_,
        audioDecCmdQueue_,
        videoDecCmdQueue_,
        audioRenderCmdQueue_,
        videoRenderCmdQueue_
    );
    SPDLOG_DEBUG("VideoPlayer constructed with state machine");

}

VideoPlayer::~VideoPlayer()
{
    stop();
    if (videoClock_) delete videoClock_;
    if (audioClock_) delete audioClock_;
    if (externalClock_) delete externalClock_;
}

void VideoPlayer::start()
{
    // 原有的初始化逻辑保持不变
    abort_ = false;
    if (!demux_.open(url_)) {
        SPDLOG_ERROR("demux open failed");
        throw std::runtime_error("demux open failed");
    }
    demux_.setPktQueue(&videoPktQueue_, &audioPktQueue_);
    demux_.setCommandQueue(&demuxCmdQueue_);
    demux_.setEventQueue(&eventQueue_);


    audioOutput_.open();
    if (!audioDec_.open(demux_.audioStream(),
                        audioOutput_.sampleRate(),
                        audioOutput_.channels())) {
        SPDLOG_ERROR("audio decoder open failed");
        throw std::runtime_error("audio decoder open failed");
    }
    audioDec_.setCommandQueue(&audioDecCmdQueue_);
    audioDec_.setEventQueue(&eventQueue_);
    audioDec_.setInputQueue(&audioPktQueue_);
    audioDec_.setOutputQueue(&audioFifo_);
    audioDec_.start();


    if (!videoDec_.open(demux_.videoStream())) {
        SPDLOG_ERROR("video decoder open failed");
        throw std::runtime_error("video decoder open failed");
    }
    videoDec_.setInputQueue(&videoPktQueue_);
    videoDec_.setOutputQueue(&videoFrameQueue_);
    videoDec_.setCommandQueue(&videoDecCmdQueue_);
    videoDec_.setEventQueue(&eventQueue_);
    videoDec_.start();

    // 启动状态机线程（必须在其他线程之前启动）
    stateMachine_->start();

    demux_.start();   // 🔥替代 demuxThread


    // 启动音频输出（内部有独立线程）
    audioOutput_.start();

    // 上报 DemuxerReady 事件
    reportEvent(DemuxerReady{});
}

void VideoPlayer::stop()
{
    // 幂等：多次调用不出事
    bool expected = false;
    if (!abort_.compare_exchange_strong(expected, true)) {
        return;
    }
    play();

    // 首先停止状态机（它会下发停止命令）
    stateMachine_->stop();
    // 等待线程退出

    demux_.stop();   // 🔥关键
    videoDec_.stop();
    audioDec_.stop();

    // 关闭队列，唤醒所有阻塞线程
    videoPktQueue_.close();
    audioPktQueue_.close();
    videoFrameQueue_.close();
    audioFifo_.close();
    eventQueue_.close();
    demuxCmdQueue_.close();
    audioDecCmdQueue_.close();
    videoDecCmdQueue_.close();
    audioRenderCmdQueue_.close();
    videoRenderCmdQueue_.close();

    // 停止并等待时钟线程退出，避免在析构/释放期间回调到已销毁的 UI
    audioOutput_.stop();
    masterClock_.stop();

    // 关闭 demux 永远不要在线程退出前 free codec。
    demux_.close();
    videoDec_.close();
    audioDec_.close();
}

void VideoPlayer::pause()
{
    // 改为推送事件，由状态机决策
    // reportEvent(PauseRequest{});
    audioOutput_.audioPause(true);
    masterClock_.pause(true);
}

void VideoPlayer::play()
{
    // reportEvent(PlayRequest{});
    audioOutput_.audioPause(false);
    masterClock_.pause(false);
}

void VideoPlayer::seek(double seconds)
{
    reportEvent(SeekRequest{seconds});
}

void VideoPlayer::startExternalClock(std::function<void (std::shared_ptr<VideoFrame>)> cb)
{
    masterClock_.setSyncType(MasterClock::SyncType::External);
    externalClock_ = new ExternalClock;
    masterClock_.setExternalClock(externalClock_);
    masterClock_.start(cb);
}

void VideoPlayer::startAudioClock(std::function<void (std::shared_ptr<VideoFrame>)> cb)
{
    masterClock_.setSyncType(MasterClock::SyncType::Audio);
    masterClock_.setAudioClock(audioClock_);
    masterClock_.start(cb);
}

void VideoPlayer::startClock(FrameCallback cb)
{
    masterClock_.setSyncType(MasterClock::SyncType::External);
    masterClock_.setExternalClock(externalClock_);
    masterClock_.start(cb);
}

void VideoPlayer::flushPackage() {
    PacketData vFlush;
    vFlush.pkt = nullptr;
    vFlush.isFlush = true;
    vFlush.serial = videoPktQueue_.serial();
    videoPktQueue_.put(std::move(vFlush), true);

    PacketData aFlush;
    aFlush.pkt = nullptr;
    aFlush.isFlush = true;
    aFlush.serial = audioPktQueue_.serial();
    audioPktQueue_.put(std::move(aFlush), true);
}

// ---------- 命令处理 ----------


// ---------- 事件上报 ----------

void VideoPlayer::reportEvent(Event&& e) {
    if (!eventQueue_.try_push(std::move(e))) {
        SPDLOG_WARN("Event queue full, dropping event: {}", eventName(e));
    }
}

// ---------- 线程循环（待改造，目前仅保留原有逻辑） ----------


