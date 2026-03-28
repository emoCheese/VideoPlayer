#include "videoplayer.h"
#include <iostream>
#include <qdebug.h>
#include <spdlog/spdlog.h>
#include "audioclock.h"
#include "externalclock.h"
#include "videoclock.h"


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
{
}

VideoPlayer::~VideoPlayer()
{
    stop();
}

void VideoPlayer::start()
{
    // 1. 打开 demux / decoder
    abort_ = false;
    if (!demux_.open(url_)) {
        SPDLOG_ERROR("demux open failed");
        throw std::runtime_error("demux open failed");
    }
    // 2 先初始化 AudioOutput 再打开 decoder
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

    // 4️ 启动线程
    audioOutput_.start();
    demuxThread_ = std::thread(&VideoPlayer::demuxLoop, this);
    audioThread_ = std::thread(&VideoPlayer::audioDecodeLoop, this);
    videoThread_ = std::thread(&VideoPlayer::videoDecodeLoop, this);
}

void VideoPlayer::stop()
{
    // 幂等：多次调用不出事
    bool expected = false;
    if (!abort_.compare_exchange_strong(expected, true)) {
        return;
    }

    // 关闭队列，唤醒所有阻塞线程
    videoPktQueue_.close();
    audioPktQueue_.close();
    videoFrameQueue_.close();
    audioFifo_.close();

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
    demux_.close();  // 如果没有，也可以删掉
    videoDec_.close();
    audioDec_.close();

    if (videoClock_) delete videoClock_;
    if (audioClock_) delete audioClock_;
    if (externalClock_) delete externalClock_;
}

void VideoPlayer::pause()
{
    masterClock_.pause(true);
    audioOutput_.audioPause(true);
}

void VideoPlayer::play()
{
    masterClock_.pause(false);
    audioOutput_.audioPause(false);
}

void VideoPlayer::seek(double seconds)
{
    audioPktQueue_.flush();
    videoPktQueue_.flush();
    audioFifo_.flush();
    videoFrameQueue_.flush();

    int serial = currentSerial();
    audioOutput_.seek(serial);
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

void VideoPlayer::demuxLoop() {
    DemuxState state = DemuxState::Init;
    while (!abort_) {
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
                break;
            }
            if (!data.pkt) {
                // 非视频包（或被丢弃）
                break;
            }
            PutStatus res;
            if (data.streamIndex == demux_.getAudioStreamIndex()) {
                data.serial = audioPktQueue_.serial();
                res = audioPktQueue_.put(std::move(data), true);    // 默认阻塞调用，不会返回 Full
            } else if (data.streamIndex == demux_.getVideoStreamIndex()) {
                data.serial = videoPktQueue_.serial();
                res = videoPktQueue_.put(std::move(data), true);
            }
            // packet queue 不能丢包，会导致编码参考链断裂

            if (res == PutStatus::Closed) {
                state = DemuxState::Ended;
                SPDLOG_DEBUG("Packet Queue Closed, DemuxState::Ended");
            }
            break; // block=true，理论上不会 Full
        }
        case DemuxState::Draining: {
            // 用 flush packet（pkt == nullptr）通知 decoder
            flushPackage();
            state = DemuxState::Ended;
            SPDLOG_INFO("Video/Audio Flush");
            break;
        }
        case DemuxState::Seeking: {
            // 预留：seek 时用
            // 典型流程：
            // 1. videoPktQueue.flush()
            // 2. demux.seek(target)
            // 3. demux.start()  // serial++
            // 4. state = Reading
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
    }
    videoPktQueue_.close();
    audioPktQueue_.close();
}

void VideoPlayer::audioDecodeLoop()
{
    while (!abort_) {
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
                    // 解码器尚未有输出，继续等待
                    continue;
                } else {
                    // FatalError 等错误情况
                    audioFifo_.close();
                    return;
                }
            }
            // 排空完成后退出线程
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
                // 如果 serial 已过期，直接丢弃
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
        PacketData pkt;
        GetStatus status = videoPktQueue_.get(pkt, true);
        if (status == GetStatus::Closed) {
            videoDec_.send(PacketData{}); // flush
            // 排空解码器
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
                    break; // 排空完成
                } else if (ret == DecodeResult::TryAgain) {
                    // 解码器尚未有输出，继续等待
                    // 避免忙等待，可以短暂休眠，但简单起见继续循环
                    continue;
                } else {
                    // Closed, FatalError, CodecError 等错误情况
                    videoFrameQueue_.close();
                    return;
                }
            }
            // 排空完成后退出线程
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
                continue; // 丢包
            }
        }
        else {
            continue;
        }

        // 循环 receive
        while (!abort_) {
            VideoFrame frame;
            // send 返回 OK，recevie 不返回 OK
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
