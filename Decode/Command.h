#ifndef COMMAND_H
#define COMMAND_H

#include <cstdint>
#include <variant>

/**
 * @brief 所有可能的命令类型，由 StateMachine 下发，各模块执行
 */

// 基础控制命令
struct CmdStart {};
struct CmdPause {};
struct CmdResume {};
struct CmdStop {};

// Seek 命令（带目标位置和新序列号）
struct CmdSeek {
    double seconds;      // 目标时间（秒）
    int serial;          // 新序列号
};

// Flush 命令（清空队列和缓冲区）
struct CmdFlush {
    int serial;          // 关联的序列号
};

// 播放速度调整
struct CmdSetSpeed {
    double rate;         // 播放速度因子（1.0 为正常）
};

/**
 * @brief 命令 variant，包含所有可能的命令类型
 */
using Command = std::variant<
    CmdStart,
    CmdPause,
    CmdResume,
    CmdStop,
    CmdSeek,
    CmdFlush,
    CmdSetSpeed
>;

/**
 * @brief 命令类型名称，用于调试
 */
inline const char* commandName(const Command& c) {
    return std::visit([](auto&& arg) -> const char* {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, CmdStart>) return "CmdStart";
        else if constexpr (std::is_same_v<T, CmdPause>) return "CmdPause";
        else if constexpr (std::is_same_v<T, CmdResume>) return "CmdResume";
        else if constexpr (std::is_same_v<T, CmdStop>) return "CmdStop";
        else if constexpr (std::is_same_v<T, CmdSeek>) return "CmdSeek";
        else if constexpr (std::is_same_v<T, CmdFlush>) return "CmdFlush";
        else if constexpr (std::is_same_v<T, CmdSetSpeed>) return "CmdSetSpeed";
        else return "Unknown";
    }, c);
}

#endif // COMMAND_H
