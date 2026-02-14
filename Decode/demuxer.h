#ifndef DEMUXER_H
#define DEMUXER_H

#include "packetqueue.h"
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}

class Demuxer {
public:
    Demuxer() = default;
    ~Demuxer();

    bool open(std::string_view u);
    void close();

    // 读一个 packet（EOF 返回 false）
    bool read(PacketData& out);

    int videoStreamIndex() const;
    int audioStreamIndex() const;

private:
    std::string url;
    AVFormatContext* fmtCtx = nullptr;
    AVPacket* pkt = nullptr;
    int videoStream = -1;
    int audioStream = -1;
    int serial = 0;
};

#endif // DEMUXER_H
