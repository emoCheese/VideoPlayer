#include "demuxer.h"


Demuxer::~Demuxer()
{
    close();
}

bool Demuxer::open(std::string_view u)
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
            if (audioStream < 0) {
                audioStream = i;
            }
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

    pkt = av_packet_alloc();
    if (!pkt) return false;
    pkt->data = nullptr;
    pkt->size = 0;
    return true;
}

void Demuxer::close()
{
    if (pkt) {
        av_packet_free(&pkt);
        pkt = nullptr;
    }
    if (fmtCtx) {
        avformat_close_input(&fmtCtx);
        avformat_free_context(fmtCtx);
        fmtCtx = nullptr;
    }

}

bool Demuxer::read(PacketData &out)
{
    while (true) {
        int ret = av_read_frame(fmtCtx, pkt);
        if (ret < 0) {
            ++serial;
            out.pkt = nullptr;
            out.serial = serial;
            return false;
        }
        if (pkt->stream_index == videoStream) {
            PacketPtr p = make_packet();
            av_packet_move_ref(p.get(), pkt);
            out.pkt = std::move(p);
            out.serial = serial;
            av_packet_unref(pkt);
            return true;
        } else if (pkt->stream_index == audioStream) {
            // todo
        }
        av_packet_unref(pkt); // 丢弃非视频包
    }
}

int Demuxer::videoStreamIndex() const
{
    return videoStream;
}

int Demuxer::audioStreamIndex() const
{
    return audioStream;
}
