#include "videoplayer.h"
#include <iostream>
#include <qdebug.h>
#include <spdlog/spdlog.h>
#include "externalclock.h"
#include "videoclock.h"

VideoPlayer::VideoPlayer(const std::string &u)
    : url(u)
    , videoPktQueue(100, 16 * 1024 * 1024)
    , videoFrameQueue(8)
    , masterClock(videoFrameQueue)
{}

VideoPlayer::~VideoPlayer()
{
    stop();
}

void VideoPlayer::start()
{
    // 1. 打开 demux / decoder
    abort_ = false;
    if (!demux.open(url)) {
        throw std::runtime_error("demux open failed");
    }
    if (!videoDec.open(demux.videoStream())) {
        throw std::runtime_error("video decoder open failed");
    }

    // 2. demux thread
    std::cout << "demux loop thread start\n";
    demuxThread = std::thread([this]() {
        demuxLoop();
        std::cout << "demux loop thread end\n";
    });
    // 3. decode thread
    // todo 音频线程启动

    // video thread
    std::cout << "video decode loop thread start\n";
    videoThread = std::thread([this]() {
        videoDecodeLoop();
        std::cout << "video decode loop thread end\n";
    });
}

void VideoPlayer::stop()
{
    // 幂等：多次调用不出事
    bool expected = false;
    if (!abort_.compare_exchange_strong(expected, true)) {
        return;
    }

    // 关闭队列，唤醒所有阻塞线程
    videoPktQueue.close();
    videoFrameQueue.close();

    // 等待线程退出
    if (demuxThread.joinable())
        demuxThread.join();
    if (videoThread.joinable())
        videoThread.join();
    // 停止并等待时钟线程退出，避免在析构/释放期间回调到已销毁的 UI
    masterClock.stop();

    // 关闭 demux 永远不要在线程退出前 free codec。
    demux.close();  // 如果没有，也可以删掉
    videoDec.close();

    if (videoClock) delete videoClock;
    if (audioClock) delete audioClock;
    if (externalClock) delete externalClock;
}

void VideoPlayer::pause()
{
    masterClock.pause(true);
}

void VideoPlayer::play()
{
    masterClock.pause(false);
}

void VideoPlayer::startClock(std::function<void (std::shared_ptr<VideoFrame>)> cb)
{
    masterClock.setSyncType(MasterClock::SyncType::External);
    externalClock = new ExternalClock;
    masterClock.setExternalClock(externalClock);
    masterClock.start(cb);
}

void VideoPlayer::demuxLoop()
{
    DemuxState state = DemuxState::Init;
    while (!abort_) {
        switch (state) {
        case DemuxState::Init: {
            demux.start();          // serial++
            videoPktQueue.start();  // serial++
            state = DemuxState::Reading;
            break;
        }
        case DemuxState::Reading: {
            PacketData data;
            bool ok = demux.readFrame(data);
            if (!ok) {
                // EOF：进入 draining
                state = DemuxState::Draining;
                break;
            }
            if (!data.pkt) {
                // 非视频包（或被丢弃）
                break;
            }
            data.serial = videoPktQueue.serial();
            auto res = videoPktQueue.put(std::move(data), true);  // 默认阻塞调用，不会返回 Full
            if (res == PutStatus::Closed) {
                state = DemuxState::Ended;
                SPDLOG_DEBUG("Video Packet Queue Closed, DemuxState::Ended");
            }
            // block=true，理论上不会 Full
            break;
        }

        case DemuxState::Draining: {
            // 用 flush packet（pkt == nullptr）通知 decoder
            PacketData flush;
            flush.pkt = nullptr;
            flush.isFlush = true;
            flush.serial = videoPktQueue.serial();

            videoPktQueue.put(std::move(flush), true);
            SPDLOG_INFO("Video Flush");
            state = DemuxState::Ended;
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
            videoPktQueue.close();
            return;
        }
        case DemuxState::Error: {
            videoPktQueue.close();
            return;
        }
        }
    }
    videoPktQueue.close();
}

/*
FrameReady → 成功
TryAgain → 需要对端操作（send 或 receive）
Drained → 解码器已完全输出完
Closed → 主动关闭
CodecError → 当前包错误，可跳过
FatalError → codecCtx 无效，必须退出
*/
void VideoPlayer::videoDecodeLoop()
{
    DecodeState state = DecodeState::ReceiveFrames;
    while (!abort_) {
        switch (state) {
        case DecodeState::ReceiveFrames: {
            VideoFrame frame;
            DecodeResult r = videoDec.receive(frame);
            switch (r) {
            case DecodeResult::FrameReady:
                if (!videoFrameQueue.push(std::move(frame))) {
                    SPDLOG_INFO("VideoFrameQueue closed, decode loop exit");
                    return;
                }
                break;
            case DecodeResult::TryAgain:
                state = DecodeState::NeedPacket;
                break;
            case DecodeResult::Drained:
                state = DecodeState::Ended;
                break;
            case DecodeResult::Closed:
                state = DecodeState::Ended;
                break;
            case DecodeResult::CodecError:
                SPDLOG_WARN("video receive codec error, continue");
                break; // 丢帧继续
            case DecodeResult::FatalError:
                SPDLOG_ERROR("video receive fatal error");
                state = DecodeState::Error;
                break;
            }
            break;
        }
        case DecodeState::NeedPacket: {
            PacketData pkt;
            auto status = videoPktQueue.get(pkt, true);
            if (status == GetStatus::Closed) {
                videoDec.send(PacketData{}); // flush
                state = DecodeState::Draining;
                break;
            }
            if (status == GetStatus::Empty)
                break;
            DecodeResult r = videoDec.send(pkt);
            switch (r) {
            case DecodeResult::FrameReady:
                state = DecodeState::ReceiveFrames;
                break;
            case DecodeResult::TryAgain:
                // send EAGAIN → 必须先 receive
                state = DecodeState::ReceiveFrames;
                break;
            case DecodeResult::CodecError:
                SPDLOG_WARN("video send codec error, skip packet");
                state = DecodeState::ReceiveFrames;
                break;
            case DecodeResult::FatalError:
                SPDLOG_ERROR("video send fatal error");
                state = DecodeState::Error;
                break;
            case DecodeResult::Closed:
                state = DecodeState::Ended;
                break;
            default:
                break;
            }
            break;
        }
        case DecodeState::Draining: {
            VideoFrame frame;
            DecodeResult r = videoDec.receive(frame);
            switch (r) {
            case DecodeResult::FrameReady:
                if (!videoFrameQueue.push(std::move(frame)))
                    return;
                break;
            case DecodeResult::Drained:
                state = DecodeState::Ended;
                break;
            case DecodeResult::TryAgain:
                // 等待内部缓冲释放
                break;
            case DecodeResult::CodecError:
                SPDLOG_WARN("drain codec error");
                break;
            case DecodeResult::FatalError:
                state = DecodeState::Error;
                break;
            case DecodeResult::Closed:
                state = DecodeState::Ended;
                break;
            }
            break;
        }
        case DecodeState::Ended:
        case DecodeState::Error:
            videoFrameQueue.close();
            return;
        }
    }
    videoFrameQueue.close();
}
