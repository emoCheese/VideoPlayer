#include "videoplayer.h"
#include <iostream>

VideoPlayer::VideoPlayer(const std::string &u, const NativeWindow &win)
    : url(u)
    , window_(win)
    , videoPktQueue(100, 16 * 1024 * 1024)
    , videoFrameQueue(8)

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
    if (!demux.open(url)) {
        throw std::runtime_error("demux open failed");
    }
    if (!videoDec.open(demux.videoStream())) {
        throw std::runtime_error("video decoder open failed");
    }

    // 2. 初始化 render（必须在 render thread 里 make current）
    renderThread = std::thread([this]() {
        if (!renderGL.initImpl(window_)) {
            return;
        }
        renderLoop();
        renderGL.cleanupImpl();
    });

    // 3. demux thread
    std::cout << "demux loop thread start\n";
    demuxThread = std::thread([this]() {
        demuxLoop();
        std::cout << "demux loop thread end\n";
    });
    // 4. decode thread
    // todo 音频线程启动
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

    // 关闭 demux（如果你有 close / interrupt）
    demux.close();  // 如果没有，也可以删掉
    videoDec.close();

    // 关闭队列，唤醒所有阻塞线程
    videoPktQueue.close();
    videoFrameQueue.close();

    // 等待线程退出
    if (demuxThread.joinable())
        demuxThread.join();
    if (videoThread.joinable())
        videoThread.join();
    if (renderThread.joinable())
        renderThread.joinable();
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
            auto res = videoPktQueue.put(std::move(data.pkt), true);

            if (std::holds_alternative<PacketQueueClosed>(res)) {
                state = DemuxState::Ended;
            }
            // block=true，理论上不会 Full
            break;
        }

        case DemuxState::Draining: {
            // ⭐ 用 flush packet（pkt == nullptr）通知 decoder
            videoPktQueue.put(nullptr);  // send nullptr = flush
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
        case DecodeState::ReceiveFrames: {  // 尽量从 decoder 掏帧
            VideoFrame frame;
            DecodeResult r = videoDec.receive(frame);
            if (r == DecodeResult::FrameReady) {
                auto ret = videoFrameQueue.push(std::move(frame), true);
                if (std::holds_alternative<FrameQueueClosed>(ret)) {
                    state = DecodeState::Ended;
                }
                break; // 继续 ReceiveFrames
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

        case DecodeState::NeedPacket: { // decoder 吃不出帧，需要 packet
            auto pktRes = videoPktQueue.get(true);

            if (std::holds_alternative<PacketQueueClosed>(pktRes)) {
                PacketData flushPkt;
                videoDec.send(flushPkt); // send(nullptr)
                state = DecodeState::Draining;
                break;
            }

            if (std::holds_alternative<PacketQueueEmpty>(pktRes)) {
                break; // 仍然 NeedPacket
            }

            PacketData pkt = std::get<PacketData>(std::move(pktRes));
            if (videoDec.send(pkt) == DecodeResult::Error) {
                state = DecodeState::Error;
            } else {
                state = DecodeState::ReceiveFrames;
            }
            break;
        }

        case DecodeState::Draining: {   // 已 send(nullptr)，等待 EOF
            VideoFrame frame;
            DecodeResult r = videoDec.receive(frame);

            if (r == DecodeResult::FrameReady) {
                videoFrameQueue.push(std::move(frame), true);
                break;
            }

            if (r == DecodeResult::Drained) {
                state = DecodeState::Ended;
                break;
            }

            if (r == DecodeResult::TryAgain) {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
                break; // 理论上很少发生
            }

            state = DecodeState::Error;
            break;
        }

        case DecodeState::Ended:
            videoFrameQueue.close();
            return;

        case DecodeState::Error:
            videoFrameQueue.close();
            return;
        }
    }
    videoFrameQueue.close();
    return;
}

void VideoPlayer::renderLoop()
{
    while (!abort_) {
        FrameResult res = videoFrameQueue.pop(true); // 阻塞式取出
        if (std::holds_alternative<FrameQueueClosed>(res)) {
            return;
        }
        if (std::holds_alternative<FrameQueueEmpty>(res)) {
            continue;
        }

        VideoFrame frame = std::get<VideoFrame>(std::move(res));

        // === 1. 计算下一帧应等待的时间 ===
        if (frame.pts != AV_NOPTS_VALUE) {
            double ptsSec =
                frame.pts * av_q2d(videoDec.timeBase());

            double wait = videoClock.delay(ptsSec);
            if (wait > 0.0) {
                std::this_thread::sleep_for(
                    std::chrono::duration<double>(wait));
            }
        }

        // === 2. 真正渲染 ===
        renderGL.renderImpl(frame);

#if defined(_WIN32)
        SwapBuffers(renderGL.hdc());
#endif
        // === 3. 告诉时钟：这一帧已经显示 ===
        if (frame.pts != AV_NOPTS_VALUE) {
            double ptsSec =
                frame.pts * av_q2d(videoDec.timeBase());
            videoClock.update(ptsSec);
        }
    }
}




FrameResult VideoPlayer::getVideoFrame(bool block)
{
    if (abort_) return FrameQueueClosed{};
    return videoFrameQueue.pop(block);
}

