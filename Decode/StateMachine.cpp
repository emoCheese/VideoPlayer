#include "StateMachine.h"
#include <chrono>

StateMachine::StateMachine(EventQueue& eventQ,
                           CommandQueue& demuxCmdQ,
                           CommandQueue& audioDecCmdQ,
                           CommandQueue& videoDecCmdQ,
                           CommandQueue& audioRenderCmdQ,
                           CommandQueue& videoRenderCmdQ)
    : eventQ_(eventQ)
    , demuxCmdQ_(demuxCmdQ)
    , audioDecCmdQ_(audioDecCmdQ)
    , videoDecCmdQ_(videoDecCmdQ)
    // , audioRenderCmdQ_(audioRenderCmdQ)
    // , videoRenderCmdQ_(videoRenderCmdQ)
    , state_(Idle{})
{
    SPDLOG_DEBUG("StateMachine constructed");
}

StateMachine::~StateMachine() {
    stop();
}

void StateMachine::start() {
    if (running_.exchange(true)) {
        SPDLOG_WARN("StateMachine already running");
        return;
    }
    thread_ = std::thread(&StateMachine::run, this);
    SPDLOG_INFO("StateMachine thread started");
}

void StateMachine::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    // 注入一个 QuitRequest 事件来唤醒可能阻塞的 pop
    injectEvent(QuitRequest{});
    if (thread_.joinable()) {
        thread_.join();
    }
    SPDLOG_INFO("StateMachine thread stopped");
}

State StateMachine::currentState() const {
    std::lock_guard lock(stateMutex_);
    return state_;
}

void StateMachine::injectEvent(Event&& e) {
    eventQ_.push(std::move(e));
}

void StateMachine::run() {
    SPDLOG_DEBUG("StateMachine run loop started");
    while (running_) {
        // 阻塞等待事件，但每 100ms 检查一次 running_ 标志
        auto ev = eventQ_.pop();
        if (!ev.has_value()) {
            // 队列已关闭
            SPDLOG_DEBUG("Event queue closed, exiting");
            break;
        }
        
        Event& e = ev.value();
        SPDLOG_TRACE("StateMachine received event: {}", eventName(e));
        
        // 处理事件并更新状态
        std::unique_lock lock(stateMutex_);
        State newState = std::visit([&](auto& s) {
            return handle(s, e);
        }, state_);
        state_ = newState;
        lock.unlock();

        // 如果新状态是 Error，可以记录错误但继续运行（或停止）
        if (std::holds_alternative<Error>(newState)) {
            SPDLOG_ERROR("StateMachine entered error state");
            // 可以根据策略决定是否停止循环
            // running_ = false;
        }
    }
    SPDLOG_DEBUG("StateMachine run loop ended");
}

// ---------- 状态处理函数 ----------
// 注意：这些函数在持有 stateMutex_ 的情况下被调用（由 run 函数保证）
// 但它们不应执行长时间阻塞的操作。

State StateMachine::handle(const Idle& s, const Event& e) {
    return std::visit([&](auto&& arg) -> State {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PlayRequest>) {
            SPDLOG_INFO("Idle -> Playing (PlayRequest)");
            dispatch(CmdStart{});
            return Playing{};
        }
        else if constexpr (std::is_same_v<T, SeekRequest>) {
            SPDLOG_WARN("Seek requested while idle, ignoring");
            return Idle{};
        }
        else if constexpr (std::is_same_v<T, QuitRequest>) {
            SPDLOG_INFO("Quit requested, staying idle");
            return Idle{};
        }
        else if constexpr (std::is_same_v<T, DemuxerReady>) {
            SPDLOG_INFO("DemuxerReady, Idle -> Playing");
            return Playing{};
        }
        else {
            SPDLOG_TRACE("Idle ignoring event: {}", eventName(e));
            return Idle{};
        }
    }, e);
}

State StateMachine::handle(const Playing& s, const Event& e) {
    return std::visit([&](auto&& arg) -> State {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PauseRequest>) {
            SPDLOG_INFO("Playing -> Paused (PauseRequest)");
            dispatch(CmdPause{});
            return Paused{};
        }
        else if constexpr (std::is_same_v<T, StopRequest>) {
            SPDLOG_INFO("Playing -> Stopped (StopRequest)");
            dispatch(CmdStop{});
            return Stopped{};
        }
        else if constexpr (std::is_same_v<T, SeekRequest>) {
            const SeekRequest& sr = std::get<SeekRequest>(e);
            int serial = generateSerial();
            SPDLOG_INFO("Playing -> Seeking (SeekRequest @{} sec, serial {})", sr.seconds, serial);
            dispatch(CmdSeek{sr.seconds, serial});
            dispatch(CmdFlush{serial});
            return Seeking{sr.seconds, serial};
        }
        else if constexpr (std::is_same_v<T, DemuxerEOF>) {
            SPDLOG_INFO("Playing -> Ended (DemuxerEOF)");
            // 可以下发停止命令给各模块
            dispatch(CmdStop{});
            return Ended{};
        }
        else if constexpr (std::is_same_v<T, QuitRequest>) {
            SPDLOG_INFO("Quit requested, stopping playback");
            dispatch(CmdStop{});
            return Stopped{};
        }
        else {
            // 忽略其他事件，保持 Playing 状态
            return Playing{};
        }
    }, e);
}

State StateMachine::handle(const Paused& s, const Event& e) {
    return std::visit([&](auto&& arg) -> State {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PlayRequest>) {
            SPDLOG_INFO("Paused -> Playing (PlayRequest)");
            dispatch(CmdResume{});
            return Playing{};
        }
        else if constexpr (std::is_same_v<T, StopRequest>) {
            SPDLOG_INFO("Paused -> Stopped (StopRequest)");
            dispatch(CmdStop{});
            return Stopped{};
        }
        else if constexpr (std::is_same_v<T, SeekRequest>) {
            const SeekRequest& sr = std::get<SeekRequest>(e);
            int serial = generateSerial();
            SPDLOG_INFO("Paused -> Seeking (SeekRequest @{} sec, serial {})", sr.seconds, serial);
            dispatch(CmdSeek{sr.seconds, serial});
            dispatch(CmdFlush{serial});
            return Seeking{sr.seconds, serial};
        }
        else if constexpr (std::is_same_v<T, QuitRequest>) {
            SPDLOG_INFO("Quit requested, stopping playback");
            dispatch(CmdStop{});
            return Stopped{};
        }
        else {
            return Paused{};
        }
    }, e);
}

State StateMachine::handle(const Seeking& s, const Event& e) {
    // 在 Seeking 状态中，我们等待 DecoderDrained 和 FirstFrameReady 事件
    return std::visit([&](auto&& arg) -> State {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, DecoderDrained>) {
            const DecoderDrained& dd = std::get<DecoderDrained>(e);
            if (dd.serial == s.serial) {
                SPDLOG_DEBUG("Seeking: received DecoderDrained for serial {}", dd.serial);
                // 继续等待 FirstFrameReady
                return Seeking{s.target, s.serial};
            } else {
                SPDLOG_TRACE("Seeking: ignoring DecoderDrained with mismatched serial {}", dd.serial);
                return Seeking{s.target, s.serial};
            }
        }
        else if constexpr (std::is_same_v<T, FirstFrameReady>) {
            const FirstFrameReady& fr = std::get<FirstFrameReady>(e);
            if (fr.serial == s.serial) {
                SPDLOG_INFO("Seeking -> Playing (FirstFrameReady for serial {})", fr.serial);
                // 可以下发恢复播放命令（如果之前是播放状态）
                dispatch(CmdResume{});
                return Playing{};
            } else {
                SPDLOG_TRACE("Seeking: ignoring FirstFrameReady with mismatched serial {}", fr.serial);
                return Seeking{s.target, s.serial};
            }
        }
        else if constexpr (std::is_same_v<T, QuitRequest>) {
            SPDLOG_INFO("Quit requested during seeking, aborting seek");
            dispatch(CmdStop{});
            return Stopped{};
        }
        else {
            // 忽略其他事件，保持 Seeking 状态
            return Seeking{s.target, s.serial};
        }
    }, e);
}

State StateMachine::handle(const Stopped& s, const Event& e) {
    return std::visit([&](auto&& arg) -> State {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PlayRequest>) {
            SPDLOG_INFO("Stopped -> Playing (PlayRequest)");
            dispatch(CmdStart{});
            return Playing{};
        }
        else if constexpr (std::is_same_v<T, QuitRequest>) {
            SPDLOG_INFO("Quit requested, staying stopped");
            return Stopped{};
        }
        else {
            return Stopped{};
        }
    }, e);
}

State StateMachine::handle(const Ended& s, const Event& e) {
    return std::visit([&](auto&& arg) -> State {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PlayRequest>) {
            SPDLOG_INFO("Ended -> Playing (PlayRequest) - restart from beginning");
            // 需要 seek 到开头并开始播放
            int serial = generateSerial();
            dispatch(CmdSeek{0.0, serial});
            dispatch(CmdFlush{serial});
            // 注意：这里直接跳转到 Seeking 状态，等待 FirstFrameReady 后再进入 Playing
            return Seeking{0.0, serial};
        }
        else if constexpr (std::is_same_v<T, QuitRequest>) {
            SPDLOG_INFO("Quit requested, staying ended");
            return Ended{};
        }
        else {
            return Ended{};
        }
    }, e);
}

State StateMachine::handle(const Error& s, const Event& e) {
    // 错误状态下，只响应 QuitRequest 或重置请求
    return std::visit([&](auto&& arg) -> State {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, StopRequest>) {
            SPDLOG_INFO("Error -> Stopped (StopRequest)");
            dispatch(CmdStop{});
            return Stopped{};
        }
        else if constexpr (std::is_same_v<T, QuitRequest>) {
            SPDLOG_INFO("Quit requested, staying in error");
            return Error{s};
        }
        else {
            return Error{s};
        }
    }, e);
}

// ---------- 命令下发 ----------

void StateMachine::dispatch(const Command& cmd) {
    // 默认广播到所有模块（实际可根据命令类型筛选）
    std::visit([&](auto&& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, CmdPause>) {
            // dispatchToAudioRender(cmd);
            // dispatchToVideoRender(cmd);
        }
        else if constexpr (std::is_same_v<T, CmdResume>) {
            // dispatchToAudioRender(cmd);
            // dispatchToVideoRender(cmd);
        }
        else if constexpr (std::is_same_v<T, CmdSeek>) {
            dispatchToDemux(cmd);
            dispatchToAudioDec(cmd);
            dispatchToVideoDec(cmd);
            // dispatchToAudioRender(cmd);
        }
    }, cmd);

    dispatchToDemux(cmd);
    dispatchToAudioDec(cmd);
    dispatchToVideoDec(cmd);
    // dispatchToAudioRender(cmd);
    // dispatchToVideoRender(cmd);
}

void StateMachine::dispatchToDemux(const Command& cmd) {
    if (!demuxCmdQ_.try_push(Command{cmd})) {
        SPDLOG_WARN("Demux command queue full, dropping command: {}", commandName(cmd));
    }
}

void StateMachine::dispatchToAudioDec(const Command& cmd) {
    if (!audioDecCmdQ_.try_push(Command{cmd})) {
        SPDLOG_WARN("AudioDec command queue full, dropping command: {}", commandName(cmd));
    }
}

void StateMachine::dispatchToVideoDec(const Command& cmd) {
    if (!videoDecCmdQ_.try_push(Command{cmd})) {
        SPDLOG_WARN("VideoDec command queue full, dropping command: {}", commandName(cmd));
    }
}

// void StateMachine::dispatchToAudioRender(const Command& cmd) {
//     if (!audioRenderCmdQ_.try_push(Command{cmd})) {
//         SPDLOG_WARN("AudioRender command queue full, dropping command: {}", commandName(cmd));
//     }
// }

// void StateMachine::dispatchToVideoRender(const Command& cmd) {
//     if (!videoRenderCmdQ_.try_push(Command{cmd})) {
//         SPDLOG_WARN("VideoRender command queue full, dropping command: {}", commandName(cmd));
//     }
// }

int StateMachine::generateSerial() {
    return ++serial_;
}
