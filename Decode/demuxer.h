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

    auto audioStream() const { return 1; }
    auto videoStream() const { return 1; }

private:
    void run();                    // 线程主循环
    void handleCommand(const Command& cmd);

    bool readFrame(PacketData& out);

private:
    std::string url_;
    AVFormatContext* fmt_ = nullptr;
    AVPacket* pkt_ = nullptr;

    PacketQueue* videoQ_ = nullptr;
    PacketQueue* audioQ_ = nullptr;

    CommandQueue* cmdQ_ = nullptr;
    EventQueue* eventQ_ = nullptr;

    std::thread thread_;
    std::atomic<bool> running_{false};
};
