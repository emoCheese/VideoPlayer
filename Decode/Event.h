#ifndef EVENT_H
#define EVENT_H

#include <cstdint>
#include <variant>
#include <string>

/**
 * @brief 所有可能的事件类型，用于 StateMachine 处理
 */

// 用户请求事件（来自 UI）
struct PlayRequest {};
struct PauseRequest {};
struct StopRequest {};
struct SeekRequest {
    double seconds;      // 目标时间（秒）
};

// Demuxer 事件
struct DemuxerReady {};          // 解复用器准备就绪
struct DemuxerEOF {};            // 文件结束
struct DemuxerError {
    std::string message;
};

// Decoder 事件
struct DecoderDrained {          // 解码器已排空（flush 完成）
    int serial;                  // 关联的序列号
};
struct FirstFrameReady {         // 解码器产出第一帧（seek 后）
    int serial;
};
struct DecoderError {
    std::string message;
    int serial;
};

// 队列状态事件
struct AudioQueueEmpty {};
struct VideoQueueEmpty {};
struct AudioQueueFull {};
struct VideoQueueFull {};

// 渲染事件
struct AudioClockUpdated {
    double pts;                  // 当前音频时钟
};
struct VideoFrameRendered {
    double pts;
};

// 系统事件
struct QuitRequest {};           // 退出请求

/**
 * @brief 事件 variant，包含所有可能的事件类型
 */
using Event = std::variant<
    PlayRequest,
    PauseRequest,
    StopRequest,
    SeekRequest,
    DemuxerReady,
    DemuxerEOF,
    DemuxerError,
    DecoderDrained,
    FirstFrameReady,
    DecoderError,
    AudioQueueEmpty,
    VideoQueueEmpty,
    AudioQueueFull,
    VideoQueueFull,
    AudioClockUpdated,
    VideoFrameRendered,
    QuitRequest
>;

/**
 * @brief 事件类型名称，用于调试
 */
inline const char* eventName(const Event& e) {
    return std::visit([](auto&& arg) -> const char* {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PlayRequest>) return "PlayRequest";
        else if constexpr (std::is_same_v<T, PauseRequest>) return "PauseRequest";
        else if constexpr (std::is_same_v<T, StopRequest>) return "StopRequest";
        else if constexpr (std::is_same_v<T, SeekRequest>) return "SeekRequest";
        else if constexpr (std::is_same_v<T, DemuxerReady>) return "DemuxerReady";
        else if constexpr (std::is_same_v<T, DemuxerEOF>) return "DemuxerEOF";
        else if constexpr (std::is_same_v<T, DemuxerError>) return "DemuxerError";
        else if constexpr (std::is_same_v<T, DecoderDrained>) return "DecoderDrained";
        else if constexpr (std::is_same_v<T, FirstFrameReady>) return "FirstFrameReady";
        else if constexpr (std::is_same_v<T, DecoderError>) return "DecoderError";
        else if constexpr (std::is_same_v<T, AudioQueueEmpty>) return "AudioQueueEmpty";
        else if constexpr (std::is_same_v<T, VideoQueueEmpty>) return "VideoQueueEmpty";
        else if constexpr (std::is_same_v<T, AudioQueueFull>) return "AudioQueueFull";
        else if constexpr (std::is_same_v<T, VideoQueueFull>) return "VideoQueueFull";
        else if constexpr (std::is_same_v<T, AudioClockUpdated>) return "AudioClockUpdated";
        else if constexpr (std::is_same_v<T, VideoFrameRendered>) return "VideoFrameRendered";
        else if constexpr (std::is_same_v<T, QuitRequest>) return "QuitRequest";
        else return "Unknown";
    }, e);
}

#endif // EVENT_H