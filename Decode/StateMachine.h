#ifndef STATEMACHINE_H
#define STATEMACHINE_H

#include "Event.h"
#include "Command.h"
#include "ThreadSafeQueue.h"
#include <atomic>
#include <thread>
#include <functional>
#include <spdlog/spdlog.h>

// 前向声明，避免循环依赖
class VideoPlayer;

/**
 * @brief 播放器状态定义
 */
struct Idle {};          // 初始状态，未开始
struct Playing {};       // 播放中
struct Paused {};        // 已暂停
struct Seeking {         // 跳转中
    double target;       // 目标时间
    int serial;          // 新序列号
};
struct Stopped {};       // 已停止（可重新开始）
struct Ended {};         // 播放结束（文件EOF）
struct Error {           // 错误状态
    std::string message;
};

using State = std::variant<
    Idle,
    Playing,
    Paused,
    Seeking,
    Stopped,
    Ended,
    Error
>;

/**
 * @brief 状态机类，作为唯一决策中心运行在独立线程
 */
class StateMachine {
public:
    using EventQueue = MPSCQueue<Event>;
    using CommandQueue = SPSCQueue<Command>;
    
    StateMachine(EventQueue& eventQ,
                 CommandQueue& demuxCmdQ,
                 CommandQueue& audioDecCmdQ,
                 CommandQueue& videoDecCmdQ,
                 CommandQueue& audioRenderCmdQ,
                 CommandQueue& videoRenderCmdQ);
    
    ~StateMachine();
    
    // 启动状态机线程
    void start();
    
    // 停止状态机线程（阻塞等待）
    void stop();
    
    // 获取当前状态（线程安全，仅用于调试）
    State currentState() const;
    
    // 手动注入事件（用于测试）
    void injectEvent(Event&& e);
    
private:
    // 状态机线程主循环
    void run();
    
    // 状态处理函数
    State handle(const Idle& s, const Event& e);
    State handle(const Playing& s, const Event& e);
    State handle(const Paused& s, const Event& e);
    State handle(const Seeking& s, const Event& e);
    State handle(const Stopped& s, const Event& e);
    State handle(const Ended& s, const Event& e);
    State handle(const Error& s, const Event& e);
    
    // 辅助函数：下发命令到指定模块
    void dispatch(const Command& cmd);
    void dispatchToDemux(const Command& cmd);
    void dispatchToAudioDec(const Command& cmd);
    void dispatchToVideoDec(const Command& cmd);
    void dispatchToAudioRender(const Command& cmd);
    void dispatchToVideoRender(const Command& cmd);
    
    // 序列号生成（用于 seek）
    int generateSerial();
    
private:
    EventQueue& eventQ_;
    CommandQueue& demuxCmdQ_;
    CommandQueue& audioDecCmdQ_;
    CommandQueue& videoDecCmdQ_;
    CommandQueue& audioRenderCmdQ_;
    CommandQueue& videoRenderCmdQ_;
    
    mutable std::mutex stateMutex_;
    State state_;                     // 当前状态，受 mutex 保护
    std::atomic<int> serial_{0};      // 序列号生成器
    
    std::thread thread_;
    std::atomic<bool> running_{false};
};

#endif // STATEMACHINE_H