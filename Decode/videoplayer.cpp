#include "videoplayer.h"
#include <iostream>
#include <qdebug.h>

VideoPlayer::VideoPlayer(const std::string &u)
    : url(u)
    , videoPktQueue(100, 16 * 1024 * 1024)
    , videoFrameQueue(8)
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

    // 关闭 demux 永远不要在线程退出前 free codec。
    demux.close();  // 如果没有，也可以删掉
    videoDec.close();
}

bool VideoPlayer::peekVideoFrame(VideoFrame *&frame)
{
    if (abort_) return false;
    return videoFrameQueue.peek(frame);
}

void VideoPlayer::popVideoFrame()
{
    videoFrameQueue.pop();
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
            auto res = videoPktQueue.put(std::move(data.pkt), data.isFlush);  // 默认阻塞调用
            if (std::holds_alternative<PacketQueueClosed>(res)) {
                state = DemuxState::Ended;
            }
            // block=true，理论上不会 Full
            break;
        }

        case DemuxState::Draining: {
            // 用 flush packet（pkt == nullptr）通知 decoder
            PacketData flush;
            flush.pkt = nullptr;
            flush.isFlush = true;
            flush.serial = demux.serial();

            videoPktQueue.put(nullptr, true);
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

void VideoPlayer::videoDecodeLoop()
{
    DecodeState state = DecodeState::ReceiveFrames;
    while (!abort_) {
        switch (state) {
        case DecodeState::ReceiveFrames: {
            VideoFrame frame;
            DecodeResult r = videoDec.receive(frame);
            if (r == DecodeResult::FrameReady) {
                // ⭐ 非阻塞 push
                if (!videoFrameQueue.push(std::move(frame))) {
                    // queue 满了 → 轻微 sleep，避免空转
                    std::this_thread::sleep_for(std::chrono::microseconds(500));
                }
                break;
            }
            if (r == DecodeResult::TryAgain) {
                state = DecodeState::NeedPacket;
                break;
            }
            if (r == DecodeResult::Drained) {
                state = DecodeState::Ended;
                break;
            }
            state = DecodeState::Error;
            break;
        }
        case DecodeState::NeedPacket: {
            auto pktRes = videoPktQueue.get(true);
            if (std::holds_alternative<PacketQueueClosed>(pktRes)) {
                videoDec.send(PacketData{}); // send(nullptr)
                state = DecodeState::Draining;
                break;
            }
            if (std::holds_alternative<PacketQueueEmpty>(pktRes)) {
                break;
            }
            PacketData pkt = std::get<PacketData>(std::move(pktRes));
            if (videoDec.send(pkt) == DecodeResult::Error) {
                state = DecodeState::Error;
                std::cerr << "VideoDecoder::send error\n" << "";
            } else {
                state = DecodeState::ReceiveFrames;
            }
            break;
        }
        case DecodeState::Draining: {
            VideoFrame frame;
            DecodeResult r = videoDec.receive(frame);
            if (r == DecodeResult::FrameReady) {
                videoFrameQueue.push(std::move(frame));
                break;
            }
            if (r == DecodeResult::Drained) {
                state = DecodeState::Ended;
                break;
            }
            if (r == DecodeResult::TryAgain) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                break;
            }
            state = DecodeState::Error;
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

