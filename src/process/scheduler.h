#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <functional>
#include <atomic>
#include <chrono>
#include <string>

#include "sync.h"

#define MAX_PROCESSES 8         // 最大进程数
#define TIME_SLICE_MS 100       // 时间片（毫秒）

enum class ProcessState {
    READY,      // 就绪
    RUNNING,    // 运行
    WAITING,    // 等待
    TERMINATED  // 终止
};

struct Process {
    uint32_t pid;                           // 进程ID
    std::string name;                       // 进程名称
    std::function<void()> task;             // 进程任务函数
    ProcessState state;                     // 进程状态
    std::unique_ptr<std::thread> thread;    // 进程线程
    std::atomic<bool> running;              // 运行标志
    uint32_t time_slice;                    // 时间片长度(ms)
    uint32_t remaining_time;                // 剩余时间片
    std::chrono::steady_clock::time_point start_time; // 开始时间

    // 添加移动构造函数
    Process(Process&& other) noexcept
        : pid(other.pid)
        , name(std::move(other.name))
        , task(std::move(other.task))
        , state(other.state)
        , thread(std::move(other.thread))
        , running(other.running.load())
        , time_slice(other.time_slice)
        , remaining_time(other.remaining_time)
        , start_time(other.start_time) {}

    // 添加移动赋值操作符
    Process& operator=(Process&& other) noexcept {
        if (this != &other) {
            pid = other.pid;
            name = std::move(other.name);
            task = std::move(other.task);
            state = other.state;
            thread = std::move(other.thread);
            running.store(other.running.load());
            time_slice = other.time_slice;
            remaining_time = other.remaining_time;
            start_time = other.start_time;
        }
        return *this;
    }

    // 禁用拷贝构造和拷贝赋值
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // 默认构造函数
    Process() : pid(0), state(ProcessState::READY), thread(nullptr),
                running(false), time_slice(0), remaining_time(0) {}
};

/**
 * 简单的时间片轮转调度器
 * 使用RR（Round Robin）调度算法
 */
class SimpleScheduler {
private:
    std::vector<Process> processes_;        // 进程列表
    std::queue<uint32_t> ready_queue_;      // 就绪队列
    mutable SimpleMutex scheduler_mutex_;    // 调度器互斥锁
    std::thread scheduler_thread_;          // 调度器线程

    std::atomic<bool> running_;             // 调度器运行标志
    uint32_t current_pid_;                  // 当前运行进程ID
    uint32_t next_pid_;                     // 下一个进程ID

    /**
     * 调度器主循环
     */
    void schedule_loop();

    /**
     * 调度下一个进程
     */
    void schedule_next();

    /**
     * 运行指定进程
     */
    void run_process(uint32_t pid);

    /**
     * 检查是否需要抢占当前进程
     */
    void check_preemption();

    /**
     * 清理已完成的进程
     */
    void cleanup_finished_processes();

public:
    SimpleScheduler();
    ~SimpleScheduler();

    // 禁止拷贝和赋值
    SimpleScheduler(const SimpleScheduler&) = delete;
    SimpleScheduler& operator=(const SimpleScheduler&) = delete;

    /**
     * 创建新进程
     * @param name 进程名称
     * @param task 进程任务函数
     * @return 进程ID，失败返回0
     */
    uint32_t create_process(const std::string& name, const std::function<void()>& task);

    /**
     * 启动调度器
     */
    void start();

    /**
     * 停止调度器
     */
    void stop();

    /**
     * 终止指定进程
     * @param pid 进程ID
     */
    void terminate_process(uint32_t pid);

    /**
     * 打印调度器状态
     */
    void print_status() const;

    /**
     * 获取进程数量
     */
    size_t get_process_count() const;

    /**
     * 获取就绪队列长度
     */
    size_t get_ready_count() const;

    /**
     * 检查调度器是否在运行
     */
    bool is_running() const;
};

#endif //SCHEDULER_H