#include "demuxer.h"


Demuxer::~Demuxer()
{
    close();
}

bool Demuxer::open(std::string_view u)
{
    m_url = u;
    close();
    m_fmtCtx = avformat_alloc_context();

    // 1. 打开文件
    if (avformat_open_input(&m_fmtCtx, m_url.c_str(), nullptr, nullptr) < 0)
        return false;

    // 2. 获取流信息
    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0)
        return false;

    // 3. 查找视频流
    m_videoStreamIndex = -1;
    for(unsigned i = 0; i < m_fmtCtx->nb_streams; ++i) {
        auto type = m_fmtCtx->streams[i]->codecpar->codec_type;
        switch (type) {
        case AVMEDIA_TYPE_VIDEO:
            if (m_videoStreamIndex < 0) {
                m_videoStreamIndex = i;
            }
            break;
        case AVMEDIA_TYPE_AUDIO:
            if (m_audioStreamIndex < 0) {
                m_audioStreamIndex = i;
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

    m_pkt = av_packet_alloc();
    if (!m_pkt) return false;
    m_pkt->data = nullptr;
    m_pkt->size = 0;
    return true;
}

void Demuxer::close()
{
    if (m_pkt) {
        av_packet_free(&m_pkt);
        m_pkt = nullptr;
    }
    if (m_fmtCtx) {
        avformat_close_input(&m_fmtCtx);
        m_fmtCtx = nullptr;
    }
}

void Demuxer::start()
{
    ++m_serial;
    if (m_pkt)
        av_packet_unref(m_pkt);
}

bool Demuxer::seek(double seconds)
{
    // to do
    return false;
}

bool Demuxer::readFrame(PacketData &out)
{
    while (true) {
        int ret = av_read_frame(m_fmtCtx, m_pkt);
        if (ret < 0) {
            ++m_serial;
            out.pkt = nullptr;
            out.serial = m_serial;
            out.isFlush = false;
            return false;
        }
        if (m_pkt->stream_index == m_videoStreamIndex) {
            PacketPtr p = make_packet();
            av_packet_move_ref(p.get(), m_pkt);
            out.pkt = std::move(p);
            out.serial = m_serial;
            out.isFlush = false;
            av_packet_unref(m_pkt);
            return true;
        } else if (m_pkt->stream_index == m_audioStreamIndex) {
            // todo
        }
        av_packet_unref(m_pkt); // 丢弃非视频包
    }
}

const AVStream *Demuxer::videoStream() const { return m_fmtCtx ? m_fmtCtx->streams[m_videoStreamIndex] : nullptr; }

const AVStream *Demuxer::audioStream() const { return m_fmtCtx ? m_fmtCtx->streams[m_audioStreamIndex] : nullptr; }

int Demuxer::getVideoStreamIndex() const { return m_videoStreamIndex; }

int Demuxer::getAudioStreamIndex() const { return m_audioStreamIndex; }

int Demuxer::serial() const { return m_serial; }
