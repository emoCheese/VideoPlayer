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

    void start();                //
    bool seek(double seconds);   //

    void run();

    void setPktQueue(PacketQueue* vq, PacketQueue* aq);


    const AVStream* videoStream() const;
    const AVStream* audioStream() const;

    inline int getVideoStreamIndex() const { return m_videoStreamIndex; };
    inline int getAudioStreamIndex() const { return m_audioStreamIndex; };

private:
    // 读一个 packet（EOF 返回 false）
    bool readFrame(PacketData& out);
    bool readVideoFrame(PacketData& out);


private:
    std::string m_url;
    AVFormatContext* m_fmtCtx = nullptr;
    AVPacket* m_pkt = nullptr;

    PacketQueue* m_videoPktQueue;
    PacketQueue* m_audioPktQueue;

    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;
};

#endif // DEMUXER_H
