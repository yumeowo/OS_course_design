#ifndef SYNC_H
#define SYNC_H

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <shared_mutex>

// 统一的锁类型定义
using SimpleMutex = std::mutex;
using RecursiveMutex = std::recursive_mutex;
using SharedMutex = std::shared_mutex;

// 统一的锁守卫定义
template<typename Mutex>
using LockGuard = std::lock_guard<Mutex>;

template<typename Mutex>
using UniqueLock = std::unique_lock<Mutex>;

// 信号量封装
class Semaphore {
private:
    SimpleMutex mutex_;
    std::condition_variable condition_;
    int count_;

public:
    explicit Semaphore(const int count = 0) : count_(count) {}

    void acquire() {
       UniqueLock<SimpleMutex> lock(mutex_);
        condition_.wait(lock, [this] { return count_ > 0; });
        --count_;
    }

    void release() {
        LockGuard<SimpleMutex> lock(mutex_);
        ++count_;
        condition_.notify_one();
    }

    bool try_acquire() {
        LockGuard<SimpleMutex> lock(mutex_);
        if (count_ > 0) {
            --count_;
            return true;
        }
        return false;
    }
};

// 读写锁封装
class ReadWriteLock {
private:
    mutable SharedMutex mutex_;

public:
    void read_lock() const { mutex_.lock_shared(); }
    void read_unlock() const { mutex_.unlock_shared(); }
    void write_lock() const { mutex_.lock(); }
    void write_unlock() const { mutex_.unlock(); }

    // RAII 守卫
    class ReadGuard {
        const ReadWriteLock& lock_;
    public:
        explicit ReadGuard(const ReadWriteLock& lock) : lock_(lock) {
            lock_.read_lock();
        }
        ~ReadGuard() { lock_.read_unlock(); }
    };

    class WriteGuard {
        ReadWriteLock& lock_;
    public:
        explicit WriteGuard(ReadWriteLock& lock) : lock_(lock) {
            lock_.write_lock();
        }
        ~WriteGuard() { lock_.write_unlock(); }
    };
};

// 自旋锁实现
class SpinLock {
private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;

public:
    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // 自旋等待
        }
    }

    void unlock() {
        flag_.clear(std::memory_order_release);
    }

    bool try_lock() {
        return !flag_.test_and_set(std::memory_order_acquire);
    }
};

// 锁管理器 - 用于统计和调试
//TODO: 最后完成统计与调试部分
class LockManager {
private:
    static std::atomic<int> lock_count_;
    static std::atomic<int> deadlock_count_;

public:
    static void register_lock() { ++lock_count_; }
    static void unregister_lock() { --lock_count_; }
    static int get_lock_count() { return lock_count_; }

    static void report_deadlock() { ++deadlock_count_; }
    static int get_deadlock_count() { return deadlock_count_; }

    static void print_statistics();
};

#endif // SYNC_H