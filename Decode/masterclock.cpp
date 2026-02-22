#include "masterclock.h"
#include <chrono>
#include <spdlog/spdlog.h>

static double getSystemTimeSec()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(
               clock::now().time_since_epoch()
               ).count();
}

void MasterClock::loop()
{
    double startTime = 0.0;          // 外部时钟基准
    double lastPts = 0.0;             // 上一帧的 PTS（用于计算 duration）
    double lastDuration = 1.0 / 25.0; // 默认帧间隔（25fps）
    int lastSerial = -1;               // 上一帧的 serial
    bool initialized = false;          // 是否已接收到第一帧

    const double maxFrameDuration = 1.0; // 防止异常跳跃

    while (running_.load()) {
        VideoFrame frame;
        if (!queue_.pop(frame)) {
            spdlog::info("MasterClock: queue closed, exit");
            break;
        }

        double pts = frame.pts;
        int serial = frame.serial;

        // ---------- 处理序列变化（如 seek） ----------
        if (!initialized || serial != lastSerial) {
            spdlog::info("MasterClock: serial changed from {} to {}, resetting clock", lastSerial, serial);
            double now = getSystemTimeSec();
            startTime = now - pts;          // 校准外部时钟
            lastPts = pts;
            lastDuration = 1.0 / 25.0;      // 重置为默认值（可根据帧率优化）
            lastSerial = serial;
            initialized = true;

            // 立即渲染该帧（不等待）
            if (callback_) {
                auto framePtr = std::make_shared<VideoFrame>(std::move(frame));
                callback_(framePtr);
            }
            // 注意：此时 lastPts 已更新为 pts，但回调后不再重复校准
            continue;
        }

        // ---------- 计算当前时间和外部时钟值 ----------
        double now = getSystemTimeSec();
        double master = now - startTime;     // 外部时钟当前值

        // ---------- 计算本帧的持续时间 ----------
        double duration = pts - lastPts;
        if (duration <= 0.0 || duration > maxFrameDuration) {
            duration = lastDuration;         // 异常时使用上一帧的间隔
            spdlog::debug("MasterClock: invalid duration {:.6f}, using last {:.6f}",
                          pts - lastPts, lastDuration);
        }
        lastDuration = duration;              // 更新供后续使用

        // ---------- 计算延迟：本帧 PTS 与外部时钟的差值 ----------
        double delay = pts - master;

        // ---------- 严重落后：丢帧 ----------
        if (delay < -0.5) {
            spdlog::warn("MasterClock: drop frame pts={:.6f} master={:.6f} delay={:.6f} duration={:.6f}",
                         pts, master, delay, duration);
            lastPts = pts;   // 更新 lastPts 以便下一帧正确计算 duration
            continue;        // 不回调，不校准 startTime
        }

        // ---------- 等待至目标时间 ----------
        if (delay > 0) {
            spdlog::debug("MasterClock: pts={:.6f} lastPts={:.6f} duration={:.6f} master={:.6f} delay={:.6f}",
                          pts, lastPts, lastDuration, master, delay);

            // 粗粒度 sleep（预留 1ms 给 busy-wait）
            if (delay > 0.002) {
                int sleep_us = static_cast<int>((delay - 0.001) * 1000000);
                spdlog::debug("MasterClock: coarse sleep {} us", sleep_us);
                std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
            }

            // 细粒度 busy-wait 直到达到目标时间
            int busy_count = 0;
            while (true) {
                now = getSystemTimeSec();
                master = now - startTime;
                if (pts - master <= 0)
                    break;
                ++busy_count;
                std::this_thread::yield();
            }
            if (busy_count > 0)
                spdlog::debug("MasterClock: fine busy-wait loops={}", busy_count);
        }

        // ---------- 等待结束，校准外部时钟（与视频时钟同步） ----------
        // 校准前先获取最新时间
        now = getSystemTimeSec();
        startTime = now - pts;   // 使外部时钟值等于 pts

        // ---------- 回调渲染 ----------
        if (callback_) {
            double cb_start = getSystemTimeSec();
            auto framePtr = std::make_shared<VideoFrame>(std::move(frame));
            callback_(framePtr);
            double cb_end = getSystemTimeSec();
            spdlog::debug("MasterClock: callback pts={:.6f} cb_time_ms={:.3f}",
                          pts, (cb_end - cb_start) * 1000.0);
        }

        lastPts = pts;   // 更新上一帧 PTS
    }
}
