#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include "videodecoder.h"

class AudioDecoder
{
public:
    AudioDecoder();

    ~AudioDecoder();

    bool open(const AVStream* stream);
    void close();

    DecodeResult send(const PacketData& pkt);
    DecodeResult receive(VideoFrame& out);

    AVRational timeBase() const { return timeBase_; }
    int streamIndex() const { return streamIndex_; }

    double getPtsSec() const;
private:

    AVRational timeBase_;
    int streamIndex_;
};

#endif // AUDIODECODER_H
