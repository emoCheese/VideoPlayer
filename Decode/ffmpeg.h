#ifndef FFMPEG_H
#define FFMPEG_H

#include "framequeue.h"
#include "packetqueue.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}

#include <string>

enum class DecodeResult {
    TryAgain,      // 没 packet / EAGAIN
    FrameReady,   // 成功解出 ≥1 帧
    Drained,      // flush 后 decoder 已空
    QueueClosed,  // packet queue 已关闭
    Error
};

class FFmpeg
{
public:
    FFmpeg();
    ~FFmpeg();

    bool open(std::string_view url);
    void close();

    // 解复用：由外部循环调用
    bool demux();

    // 解码：由外部循环调用
    DecodeResult decodeVideo();

    // seek
    bool seek(double seconds);

private:
    bool openVideoDecode();
    bool openAudioDecode();

    void flushVideoDecoder();


private:
    std::string url;
    AVPacket* demuxPkt = nullptr;   // ⭐ 栈对象
    // =====================
    // 视频
    // =====================

    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* videoCodecCtx = nullptr;
    AVFrame* videoFrame = nullptr;
    SwsContext* swsCtx = nullptr;
    int width = 0;
    int height = 0;
    uint8_t* nv12Buffer = nullptr;

    int videoStream = -1;
    PacketQueue videoPktQueue;   // 核心
    FrameQueue videoFrameQueue;


    // =====================
    // 音频 to do
    // =====================
};

#endif // FFMPEG_H
