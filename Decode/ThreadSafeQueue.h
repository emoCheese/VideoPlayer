#ifndef THREADSAFEQUEUE_H
#define THREADSAFEQUEUE_H

#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
#include <cassert>
#include <optional>

namespace detail {
    // 空标记类型，用于区分 SPSC 和 MPSC（目前实现相同）
    struct SPSC {};
    struct MPSC {};
}

/**
 * @brief 线程安全队列模板，支持 SPSC 和 MPSC 模式
 * 
 * @tparam T 元素类型
 * @tparam Mode 队列模式：detail::SPSC 或 detail::MPSC（默认 MPSC）
 * @tparam Capacity 固定容量（0 表示动态容量，仅当 Storage 为 RingBuffer 时有效）
 */
template<typename T, typename Mode = detail::MPSC, size_t Capacity = 0>
class ThreadSafeQueue {
public:
    using value_type = T;
    
    /**
     * @brief 构造队列，若 Capacity==0 则使用动态容量（由 max_capacity 指定）
     */
    explicit ThreadSafeQueue(size_t max_capacity = 1024)
        : max_capacity_(Capacity > 0 ? Capacity : max_capacity)
        , buffer_(max_capacity_)
    {
        assert(max_capacity_ > 0);
    }
    
    ~ThreadSafeQueue() {
        close();
    }
    
    /**
     * @brief 阻塞 push，队列满时等待
     * @return true 成功，false 队列已关闭
     */
    bool push(T&& item) {
        std::unique_lock lock(mutex_);
        not_full_.wait(lock, [&] { return closed_ || size_ < max_capacity_; });
        if (closed_) return false;
        
        buffer_[windex_] = std::move(item);
        windex_ = (windex_ + 1) % max_capacity_;
        ++size_;
        not_empty_.notify_one();
        return true;
    }
    
    /**
     * @brief 非阻塞 push
     * @return true 成功，false 队列满或已关闭
     */
    bool try_push(T&& item) {
        std::lock_guard lock(mutex_);
        if (closed_ || size_ >= max_capacity_) return false;
        
        buffer_[windex_] = std::move(item);
        windex_ = (windex_ + 1) % max_capacity_;
        ++size_;
        not_empty_.notify_one();
        return true;
    }
    
    /**
     * @brief 阻塞 pop，队列空时等待
     * @return std::optional<T> 有值表示成功，无值表示队列已关闭且为空
     */
    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        not_empty_.wait(lock, [&] { return closed_ || size_ > 0; });
        if (size_ == 0) {
            // closed_ 必须为 true
            return std::nullopt;
        }
        
        T item = std::move(buffer_[rindex_]);
        rindex_ = (rindex_ + 1) % max_capacity_;
        --size_;
        not_full_.notify_one();
        return item;
    }
    
    /**
     * @brief 非阻塞 pop
     * @return std::optional<T> 有值表示成功，无值表示队列空或已关闭
     */
    std::optional<T> try_pop() {
        std::lock_guard lock(mutex_);
        if (size_ == 0) return std::nullopt;
        
        T item = std::move(buffer_[rindex_]);
        rindex_ = (rindex_ + 1) % max_capacity_;
        --size_;
        not_full_.notify_one();
        return item;
    }
    
    /**
     * @brief 清空队列（用于 seek 等场景）
     */
    void flush() {
        std::lock_guard lock(mutex_);
        // 简单重置索引，元素析构依赖 T 的析构函数（在覆盖时发生）
        rindex_ = 0;
        windex_ = 0;
        size_ = 0;
        // 唤醒所有等待线程
        not_full_.notify_all();
        not_empty_.notify_all();
    }
    
    /**
     * @brief 关闭队列，唤醒所有等待线程，之后所有 push/pop 操作将失败
     */
    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        not_full_.notify_all();
        not_empty_.notify_all();
    }
    
    size_t size() const {
        std::lock_guard lock(mutex_);
        return size_;
    }
    
    bool empty() const {
        std::lock_guard lock(mutex_);
        return size_ == 0;
    }
    
    bool closed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }
    
    size_t capacity() const {
        return max_capacity_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    
    const size_t max_capacity_;
    std::vector<T> buffer_;          // 环形缓冲区
    size_t rindex_ = 0;              // 读位置
    size_t windex_ = 0;              // 写位置
    size_t size_ = 0;                // 当前元素数
    bool closed_ = false;
};

// 特化：动态容量队列（Capacity == 0）
template<typename T, typename Mode>
class ThreadSafeQueue<T, Mode, 0> {
public:
    explicit ThreadSafeQueue(size_t max_capacity = 1024)
        : max_capacity_(max_capacity)
        , buffer_(max_capacity)
    {
        assert(max_capacity_ > 0);
    }
    
    // 其余方法与上述相同（可复用代码，但为简化重复实现）
    // 实际工程中应提取公共基类，这里为了清晰直接重复
    
    bool push(T&& item) {
        std::unique_lock lock(mutex_);
        not_full_.wait(lock, [&] { return closed_ || size_ < max_capacity_; });
        if (closed_) return false;
        
        buffer_[windex_] = std::move(item);
        windex_ = (windex_ + 1) % max_capacity_;
        ++size_;
        not_empty_.notify_one();
        return true;
    }
    
    bool try_push(T&& item) {
        std::lock_guard lock(mutex_);
        if (closed_ || size_ >= max_capacity_) return false;
        
        buffer_[windex_] = std::move(item);
        windex_ = (windex_ + 1) % max_capacity_;
        ++size_;
        not_empty_.notify_one();
        return true;
    }
    
    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        not_empty_.wait(lock, [&] { return closed_ || size_ > 0; });
        if (size_ == 0) return std::nullopt;
        
        T item = std::move(buffer_[rindex_]);
        rindex_ = (rindex_ + 1) % max_capacity_;
        --size_;
        not_full_.notify_one();
        return item;
    }
    
    std::optional<T> try_pop() {
        std::lock_guard lock(mutex_);
        if (size_ == 0) return std::nullopt;
        
        T item = std::move(buffer_[rindex_]);
        rindex_ = (rindex_ + 1) % max_capacity_;
        --size_;
        not_full_.notify_one();
        return item;
    }
    
    void flush() {
        std::lock_guard lock(mutex_);
        rindex_ = 0;
        windex_ = 0;
        size_ = 0;
        not_full_.notify_all();
        not_empty_.notify_all();
    }
    
    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        not_full_.notify_all();
        not_empty_.notify_all();
    }
    
    size_t size() const {
        std::lock_guard lock(mutex_);
        return size_;
    }
    
    bool empty() const {
        std::lock_guard lock(mutex_);
        return size_ == 0;
    }
    
    bool closed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }
    
    size_t capacity() const {
        return max_capacity_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    
    const size_t max_capacity_;
    std::vector<T> buffer_;
    size_t rindex_ = 0;
    size_t windex_ = 0;
    size_t size_ = 0;
    bool closed_ = false;
};

// 别名定义
template<typename T>
using SPSCQueue = ThreadSafeQueue<T, detail::SPSC, 0>;

template<typename T>
using MPSCQueue = ThreadSafeQueue<T, detail::MPSC, 0>;

#endif // THREADSAFEQUEUE_H