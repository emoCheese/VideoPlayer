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
    audioOutput_.open();
    if (!audioDec_.open(demux_.audioStream(),
                        audioOutput_.sampleRate(),
                        audioOutput_.channels())) {
        SPDLOG_ERROR("audio decoder open failed");
        throw std::runtime_error("audio decoder open failed");
    }
    if (!videoDec_.open(demux_.videoStream())) {
        SPDLOG_ERROR("video decoder open failed");
        throw std::runtime_error("video decoder open failed");
    }

    // 启动状态机线程（必须在其他线程之前启动）
    stateMachine_->start();

    // 启动音频输出（内部有独立线程）
    audioOutput_.start();

    // 启动数据流线程
    demuxThread_ = std::thread(&VideoPlayer::demuxLoop, this);
    audioThread_ = std::thread(&VideoPlayer::audioDecodeLoop, this);
    videoThread_ = std::thread(&VideoPlayer::videoDecodeLoop, this);

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

    // 首先停止状态机（它会下发停止命令）
    stateMachine_->stop();

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

    // 等待线程退出
    if (demuxThread_.joinable())
        demuxThread_.join();
    if (videoThread_.joinable())
        videoThread_.join();
    if (audioThread_.joinable())
        audioThread_.join();

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
    reportEvent(PauseRequest{});
}

void VideoPlayer::play()
{
    reportEvent(PlayRequest{});
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

void VideoPlayer::handleDemuxCommand(const Command& cmd) {
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, CmdStart>) {
            // Demux 开始读取（serial 已在 start 中增加）
            SPDLOG_DEBUG("Demux received CmdStart");
        }
        else if constexpr (std::is_same_v<T, CmdPause>) {
            // Demux 暂停（暂不实现，因为 demux 线程可以继续读，但 packet 队列会满）
            SPDLOG_DEBUG("Demux received CmdPause");
        }
        else if constexpr (std::is_same_v<T, CmdSeek>) {
            const CmdSeek& cs = std::get<CmdSeek>(cmd);
            SPDLOG_INFO("Demux seek to {} sec, serial {}", cs.seconds, cs.serial);
            // 执行 seek 操作
            demux_.seek(cs.seconds);
            // 更新 packet queue serial
            videoPktQueue_.start(); // 内部会增加 serial
            audioPktQueue_.start();
            // 上报事件（可选）
            reportEvent(DecoderDrained{cs.serial});
        }
        else if constexpr (std::is_same_v<T, CmdFlush>) {
            const CmdFlush& cf = std::get<CmdFlush>(cmd);
            SPDLOG_DEBUG("Demux flush with serial {}", cf.serial);
            // 刷新 packet 队列（已在 seek 中处理）
        }
        else {
            SPDLOG_TRACE("Demux ignoring command: {}", commandName(cmd));
        }
    }, cmd);
}

void VideoPlayer::handleAudioDecCommand(const Command& cmd) {
    // 类似 handleDemuxCommand，处理音频解码器相关命令
    // 简化：仅处理 flush
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, CmdFlush>) {
            const CmdFlush& cf = std::get<CmdFlush>(cmd);
            SPDLOG_DEBUG("AudioDecoder flush with serial {}", cf.serial);
            audioDec_.send(PacketData{});
            // 上报排空完成
            reportEvent(DecoderDrained{cf.serial});
        }
        else if constexpr (std::is_same_v<T, CmdPause>) {
            // 暂停解码（暂不实现）
        }
        else {
            // 忽略
        }
    }, cmd);
}

void VideoPlayer::handleVideoDecCommand(const Command& cmd) {
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, CmdFlush>) {
            const CmdFlush& cf = std::get<CmdFlush>(cmd);
            SPDLOG_DEBUG("VideoDecoder flush with serial {}", cf.serial);
            videoDec_.send(PacketData{});
            reportEvent(DecoderDrained{cf.serial});
        }
    }, cmd);
}

void VideoPlayer::handleAudioRenderCommand(const Command& cmd) {
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, CmdPause>) {
            audioOutput_.audioPause(true);
        }
        else if constexpr (std::is_same_v<T, CmdResume>) {
            audioOutput_.audioPause(false);
        }
        else if constexpr (std::is_same_v<T, CmdFlush>) {
            const CmdFlush& cf = std::get<CmdFlush>(cmd);
            audioOutput_.seek(cf.serial);
        }
        else if constexpr (std::is_same_v<T, CmdStop>) {
            // 停止音频输出（由 stop 统一处理）
        }
    }, cmd);
}

void VideoPlayer::handleVideoRenderCommand(const Command& cmd) {
    // 视频渲染命令（目前由 UI 线程处理，暂不实现）
    SPDLOG_TRACE("VideoRender command: {}", commandName(cmd));
}

// ---------- 事件上报 ----------

void VideoPlayer::reportEvent(Event&& e) {
    if (!eventQueue_.try_push(std::move(e))) {
        SPDLOG_WARN("Event queue full, dropping event: {}", eventName(e));
    }
}

// ---------- 线程循环（待改造，目前仅保留原有逻辑） ----------

void VideoPlayer::demuxLoop() {
    DemuxState state = DemuxState::Init;
    while (!abort_) {
        // 1. 处理命令（优先级高）
        while (auto cmd = demuxCmdQueue_.try_pop()) {
            handleDemuxCommand(*cmd);
        }

        // 2. 处理数据（可阻塞）
        switch (state) {
        case DemuxState::Init: {
            demux_.start();         // serial++
            videoPktQueue_.start(); // serial++
            audioPktQueue_.start();
            state = DemuxState::Reading;
            break;
        }
        case DemuxState::Reading: {
            PacketData data;
            bool ok = demux_.readFrame(data);
            if (!ok) {
                // EOF：进入 draining
                state = DemuxState::Draining;
                reportEvent(DemuxerEOF{});
                break;
            }
            if (!data.pkt) {
                // 非视频包（或被丢弃）
                break;
            }
            PutStatus res;
            if (data.streamIndex == demux_.getAudioStreamIndex()) {
                data.serial = audioPktQueue_.serial();
                res = audioPktQueue_.put(std::move(data), true);
            } else if (data.streamIndex == demux_.getVideoStreamIndex()) {
                data.serial = videoPktQueue_.serial();
                res = videoPktQueue_.put(std::move(data), true);
            }
            if (res == PutStatus::Closed) {
                state = DemuxState::Ended;
                SPDLOG_DEBUG("Packet Queue Closed, DemuxState::Ended");
            }
            break;
        }
        case DemuxState::Draining: {
            flushPackage();
            state = DemuxState::Ended;
            SPDLOG_INFO("Video/Audio Flush");
            break;
        }
        case DemuxState::Seeking: {
            // 预留：seek 时用
            break;
        }
        case DemuxState::Ended: {
            videoPktQueue_.close();
            audioPktQueue_.close();
            return;
        }
        case DemuxState::Error: {
            videoPktQueue_.close();
            audioPktQueue_.close();
            return;
        }
        }

        // 3. 上报事件（如有需要）
        // 已在代码中上报
    }
    videoPktQueue_.close();
    audioPktQueue_.close();
}

void VideoPlayer::audioDecodeLoop()
{
    while (!abort_) {
        // 处理命令
        while (auto cmd = audioDecCmdQueue_.try_pop()) {
            handleAudioDecCommand(*cmd);
        }

        // 原有数据逻辑
        PacketData data;
        GetStatus status = audioPktQueue_.get(data, true);
        if (status == GetStatus::Closed) {
            audioDec_.send(PacketData{}); // flush decoder
            // 排空解码器
            while (!abort_) {
                AudioBlock block;
                DecodeResult ret = audioDec_.receive(block);
                if (ret == DecodeResult::FrameReady) {
                    block.serial = audioPktQueue_.serial(); // 使用当前serial
                    if (!audioFifo_.push(std::move(block)))
                        break;
                    continue;
                } else if (ret == DecodeResult::Drained) {
                    break; // 排空完成
                } else if (ret == DecodeResult::TryAgain) {
                    continue;
                } else {
                    audioFifo_.close();
                    return;
                }
            }
            audioFifo_.close();
            return;
        }
        if (status != GetStatus::Ok)
            continue;
        int pkt_serial = data.serial;
        DecodeResult sret = audioDec_.send(data);
        if (sret == DecodeResult::FatalError)
            break;
        while (!abort_) {
            AudioBlock block;
            DecodeResult ret = audioDec_.receive(block);
            if (ret == DecodeResult::FrameReady) {
                block.serial = pkt_serial;
                if (block.serial != audioPktQueue_.serial())
                    continue;
                if (!audioFifo_.push(std::move(block)))
                    break;
                continue;
            }
            if (ret == DecodeResult::TryAgain)
                break;
            if (ret == DecodeResult::Drained)
                break;
            if (ret == DecodeResult::FatalError) {
                audioFifo_.close();
                return;
            }
        }
    }
    audioFifo_.close();
}

void VideoPlayer::videoDecodeLoop()
{
    int cur_serial = -1;
    while (!abort_) {
        // 处理命令
        while (auto cmd = videoDecCmdQueue_.try_pop()) {
            handleVideoDecCommand(*cmd);
        }

        // 原有数据逻辑
        PacketData pkt;
        GetStatus status = videoPktQueue_.get(pkt, true);
        if (status == GetStatus::Closed) {
            videoDec_.send(PacketData{}); // flush
            while (!abort_) {
                VideoFrame frame;
                DecodeResult ret = videoDec_.receive(frame);
                if (ret == DecodeResult::FrameReady) {
                    frame.serial = cur_serial;
                    if (!videoFrameQueue_.push(std::move(frame))) {
                        videoFrameQueue_.close();
                        return;
                    }
                    continue;
                } else if (ret == DecodeResult::Drained) {
                    break;
                } else if (ret == DecodeResult::TryAgain) {
                    continue;
                } else {
                    videoFrameQueue_.close();
                    return;
                }
            }
            videoFrameQueue_.close();
            return;
        }
        else if (status == GetStatus::Ok) {
            cur_serial = pkt.serial;
            DecodeResult r = videoDec_.send(pkt);
            if (r == DecodeResult::FatalError) {
                videoFrameQueue_.close();
                return;
            }
            if (r == DecodeResult::CodecError) {
                continue;
            }
        }
        else {
            continue;
        }

        while (!abort_) {
            VideoFrame frame;
            DecodeResult ret = videoDec_.receive(frame);
            switch (ret) {
            case DecodeResult::FrameReady:
                frame.serial = cur_serial;
                if (!videoFrameQueue_.push(std::move(frame))) {
                    videoFrameQueue_.close();
                    return;
                }
                continue;

            case DecodeResult::TryAgain:
                break;

            case DecodeResult::Drained:
            case DecodeResult::Closed:
            case DecodeResult::FatalError:
                videoFrameQueue_.close();
                return;

            case DecodeResult::CodecError:
                break;
            }
            break;
        }
    }
    videoFrameQueue_.close();
}
