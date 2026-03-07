#ifndef VIDEODECODER_H
#define VIDEODECODER_H

#include <atomic>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}
#include "packetqueue.h"
#include "framequeue.h"

enum class DecodeResult {
    Ok,            // send 成功（packet accepted）
    TryAgain,      // EAGAIN
    FrameReady,    // receive 成功输出一帧
    Drained,       // receive 返回 EOF
    Closed,
    CodecError,
    FatalError
};

class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();

    bool open(const AVStream* stream);
    void close();

    DecodeResult send(const PacketData& pkt);
    DecodeResult receive(VideoFrame& out);

    AVRational timeBase() const { return timeBase_; }
    int streamIndex() const { return streamIndex_; }

    // double getPtsSec() const;

private:
    AVCodecContext* codecCtx = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* swsCtx = nullptr;

    int width = 0;
    int height = 0;
    AVPixelFormat srcPixFmt = AV_PIX_FMT_NONE;

    uint8_t* nv12Buffer = nullptr;

    AVRational timeBase_{};
    int streamIndex_ = -1;

    std::atomic<bool> closed_{false};
};


#endif // VIDEODECODER_H
