#ifndef AUDIORINGBUFFER_H
#define AUDIORINGBUFFER_H

#include <atomic>
#include <cstddef>
#include <vector>

class AudioRingBuffer
{
public:
    explicit AudioRingBuffer(size_t capacitySamples)
        : capacity_(capacitySamples),
        buffer_(capacitySamples)
    {
        readIndex_.store(0, std::memory_order_relaxed);
        writeIndex_.store(0, std::memory_order_relaxed);
    }

    // 写入 samples（生产者线程）
    size_t write(const float* data, size_t samples)
    {
        size_t read  = readIndex_.load(std::memory_order_acquire);
        size_t write = writeIndex_.load(std::memory_order_relaxed);

        size_t free = capacity_ - (write - read);
        size_t toWrite = std::min(samples, free);

        for (size_t i = 0; i < toWrite; ++i)
        {
            buffer_[(write + i) % capacity_] = data[i];
        }

        writeIndex_.store(write + toWrite, std::memory_order_release);
        return toWrite;
    }

    // 读取 samples（音频回调线程）
    size_t read(float* out, size_t samples)
    {
        size_t write = writeIndex_.load(std::memory_order_acquire);
        size_t read  = readIndex_.load(std::memory_order_relaxed);

        size_t available = write - read;
        size_t toRead = std::min(samples, available);

        for (size_t i = 0; i < toRead; ++i)
        {
            out[i] = buffer_[(read + i) % capacity_];
        }

        readIndex_.store(read + toRead, std::memory_order_release);
        return toRead;
    }

    void flush()
    {
        readIndex_.store(0, std::memory_order_relaxed);
        writeIndex_.store(0, std::memory_order_relaxed);
    }

    size_t available() const
    {
        size_t write = writeIndex_.load(std::memory_order_acquire);
        size_t read  = readIndex_.load(std::memory_order_acquire);
        return write - read;
    }

    size_t freeSpace() const
    {
        return capacity_ - available();
    }

private:
    const size_t capacity_;
    std::vector<float> buffer_;

    std::atomic<size_t> readIndex_;
    std::atomic<size_t> writeIndex_;
};

#endif // AUDIORINGBUFFER_H
