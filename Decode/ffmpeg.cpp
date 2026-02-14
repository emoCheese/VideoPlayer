#include "ffmpeg.h"
#include "videodecoder.h"


extern "C" {
}

FFmpeg::FFmpeg()
    : videoFrameQueue(16) // 和 ffplay 默认接近
    , videoPktQueue()
{}

FFmpeg::~FFmpeg()
{
    close();
}

bool FFmpeg::open(std::string_view u)
{
    url = u;
    close();
    fmtCtx = avformat_alloc_context();

    // 1. 打开文件
    if (avformat_open_input(&fmtCtx, url.c_str(), nullptr, nullptr) < 0)
        return false;

    // 2. 获取流信息
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0)
        return false;

     // 3. 查找视频流
    videoStream = -1;
    for(unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
        auto type = fmtCtx->streams[i]->codecpar->codec_type;
        switch (type) {
        case AVMEDIA_TYPE_VIDEO:
            if (videoStream < 0) {
                videoStream = i;
            }
            break;
        case AVMEDIA_TYPE_AUDIO:
            break;

        case AVMEDIA_TYPE_SUBTITLE:

            break;
        case AVMEDIA_TYPE_UNKNOWN:
            break;
        case AVMEDIA_TYPE_DATA:
        case AVMEDIA_TYPE_ATTACHMENT:
        case AVMEDIA_TYPE_NB:
            break;
        }
    }

    demuxPkt = av_packet_alloc();
    if (!demuxPkt) return false;
    demuxPkt->data = nullptr;
    demuxPkt->size = 0;

    if (!openVideoDecode()) {
        return false;
    }
    return true;
}



void FFmpeg::close()
{
    if (demuxPkt) {
        av_packet_free(&demuxPkt);
        demuxPkt = nullptr;
    }
    // =============================
    // 关闭视频解码器
    // =============================
    if (videoCodecCtx) {
        avcodec_free_context(&videoCodecCtx);
        videoCodecCtx = nullptr;
    }

    if (videoFrame) {
        av_frame_free(&videoFrame);
        videoFrame = nullptr;
    }

    if (swsCtx) {
        sws_freeContext(swsCtx);
        swsCtx = nullptr;
    }

    if (nv12Buffer) {
        av_free(nv12Buffer);
        nv12Buffer = nullptr;
    }
}

bool FFmpeg::demux()
{
    if (!demuxPkt) return false;

    int ret = av_read_frame(fmtCtx, demuxPkt);
    if (ret < 0) {
        // EOF：送一个 null packet
        videoPktQueue.put(make_packet());
        av_packet_free(&demuxPkt);
        return false;
    }

    if (demuxPkt->stream_index == videoStream) {
        PacketPtr p = make_packet();
        av_packet_move_ref(p.get(), demuxPkt);
        videoPktQueue.put(std::move(p));
    }

    av_packet_unref(demuxPkt);
    return true;
}

DecodeResult FFmpeg::decodeVideo()
{
    // 1. 非阻塞取 packet
    auto result = videoPktQueue.get(false);

    if (std::holds_alternative<QueueEmpty>(result)) {
        return DecodeResult::TryAgain;
    }

    if (std::holds_alternative<QueueClosed>(result)) {
        return DecodeResult::QueueClosed;
    }

    auto& data = std::get<PacketData>(result);

    // 2. send_packet
    int ret = 0;
    if (!data.pkt || data.pkt->data == nullptr) {
        // flush
        ret = avcodec_send_packet(videoCodecCtx, nullptr);
    } else {
        ret = avcodec_send_packet(videoCodecCtx, data.pkt.get());
    }

    if (ret == AVERROR(EAGAIN)) {
        // decoder 内部还有 frame 没取完
        // 继续 receive_frame
    } else if (ret < 0) {
        return DecodeResult::Error;
    }

    // 3. receive_frame（非阻塞拉干净）
    bool gotFrame = false;

    while (true) {
        ret = avcodec_receive_frame(videoCodecCtx, videoFrame);
        if (ret == 0) {
            // 转 NV12
            uint8_t* dst[2] = {
                nv12Buffer,
                nv12Buffer + width * height
            };
            int linesize[2] = { width, width };
            sws_scale(
                swsCtx, videoFrame->data, videoFrame->linesize,
                0, height, dst,
                linesize
                );

            // 构造 VideoFrame（这是解码器的“产品”）
            VideoFrame out;
            out.width  = width;
            out.height = height;
            out.format = AV_PIX_FMT_NV12;
            out.pts    = videoFrame->best_effort_timestamp;
            out.serial = data.serial;

            out.data.resize(width * height * 3 / 2);
            memcpy(out.data.data(), nv12Buffer, out.data.size());
            av_frame_unref(videoFrame);
            gotFrame = true;
            continue;
        }

        if (ret == AVERROR(EAGAIN))
            break;
        if (ret == AVERROR_EOF)
            return DecodeResult::Drained;
        return DecodeResult::Error;
    }
    return gotFrame ? DecodeResult::FrameReady : DecodeResult::TryAgain;
}

bool FFmpeg::openVideoDecode()
{
    // 4. 获取解码器
    AVCodecParameters* par = fmtCtx->streams[videoStream]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        close();
        return false;
    }
    // 5. 创建解码器上下文s
    videoCodecCtx = avcodec_alloc_context3(codec);
    if (!videoCodecCtx) {
        close();
        return false;
    }
    // 6. 从流参数复制配置
    if (avcodec_parameters_to_context(videoCodecCtx, par) < 0) {
        close();
        return false;
    }

    // 7. 打开解码器
    if (avcodec_open2(videoCodecCtx, codec, nullptr) < 0) {
        close();
        return false;
    }

    // 8. 初始化帧
    videoFrame = av_frame_alloc();
    if (!videoFrame) {
        close();
        return false;
    }

    // 9. 获取帧尺寸
    width = videoCodecCtx->width;
    height = videoCodecCtx->height;

    // 10. 创建转换上下文 (YUV420P -> NV12)
    swsCtx = sws_getContext(
        width, height, AV_PIX_FMT_YUV420P,
        width, height, AV_PIX_FMT_NV12,
        SWS_BILINEAR, nullptr, nullptr, nullptr
        );
    if (!swsCtx) {
        close();
        return false;
    }

    // 11. 分配NV12缓冲区
    nv12Buffer = (uint8_t*)av_malloc(width * height * 3 / 2);
    if (!nv12Buffer) {
        close();
        return false;
    }
    return true;
}

bool FFmpeg::openAudioDecode()
{

    return false;
}

