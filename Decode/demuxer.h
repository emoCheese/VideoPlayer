#pragma once

#include "packetqueue.h"
#include "ThreadSafeQueue.h"
#include "Command.h"
#include "Event.h"

#include <thread>
#include <atomic>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
}

class Demuxer {
public:
    using CommandQueue = SPSCQueue<Command>;
    using EventQueue   = MPSCQueue<Event>;

    Demuxer();
    ~Demuxer();

    bool open(std::string_view url);
    void close();

    void start();     // 启动线程
    void stop();      // 停止线程

    void setPktQueue(PacketQueue* vq, PacketQueue* aq);
    void setCommandQueue(CommandQueue* q);
    void setEventQueue(EventQueue* q);


    inline const AVStream *videoStream() const { return fmt_ ? fmt_->streams[m_videoStreamIndex] : nullptr; }

    inline const AVStream *audioStream() const { return fmt_ ? fmt_->streams[m_audioStreamIndex] : nullptr; }


    inline int getVideoStreamIndex() const { return m_videoStreamIndex; };
    inline int getAudioStreamIndex() const { return m_audioStreamIndex; };

private:
    void run();                    // 线程主循环
    void handleCommand(const Command& cmd);

    bool readFrame(PacketData& out);

private:
    std::string url_;
    AVFormatContext* fmt_ = nullptr;
    AVPacket* pkt_ = nullptr;

    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;

    PacketQueue* videoQ_ = nullptr;
    PacketQueue* audioQ_ = nullptr;

    CommandQueue* cmdQ_ = nullptr;
    EventQueue* eventQ_ = nullptr;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
};
