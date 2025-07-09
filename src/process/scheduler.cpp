#include "scheduler.h"

#include <iostream>
#include <algorithm>
#include <chrono>

SimpleScheduler::SimpleScheduler()
    : running_(false), current_pid_(0), next_pid_(1) {
    processes_.reserve(MAX_PROCESSES);  // 预留容量，避免重新分配
}

SimpleScheduler::~SimpleScheduler() {
    stop();
}

uint32_t SimpleScheduler::create_process(const std::string& name, const std::function<void()>& task) {
    LockGuard<SimpleMutex> lock(scheduler_mutex_);

    // 检查进程数限制
    if (processes_.size() >= MAX_PROCESSES) {
        std::cerr << "无法创建进程: 已达到最大进程数限制 " << MAX_PROCESSES << std::endl;
        return 0;
    }

    const uint32_t pid = next_pid_++;

    // 直接在容器中构造，避免移动
    processes_.emplace_back();
    Process& process = processes_.back();

    process.pid = pid;
    process.name = name;
    process.task = task;
    process.state = ProcessState::READY;
    process.time_slice = TIME_SLICE_MS;
    process.remaining_time = TIME_SLICE_MS;
    process.running = false;
    process.thread = nullptr;  // 初始化为nullptr

    ready_queue_.push(pid);

    std::cout << "创建进程: " << name << " (PID: " << pid << ")" << std::endl;
    return pid;
}

void SimpleScheduler::start() {
    LockGuard<SimpleMutex> lock(scheduler_mutex_);

    if (running_) {
        return;
    }

    running_ = true;
    scheduler_thread_ = std::thread([this]() {
        this->schedule_loop();
    });

    std::cout << "调度器启动" << std::endl;
}

void SimpleScheduler::stop() {
    {
        LockGuard<SimpleMutex> lock(scheduler_mutex_);
        running_ = false;
    }

    // 等待所有进程完成
    for (auto& process : processes_) {
        if (process.thread && process.thread->joinable()) {
            process.running = false;
            process.thread->join();
        }
    }

    // 等待调度器线程完成
    if (scheduler_thread_.joinable()) {
        scheduler_thread_.join();
    }

    std::cout << "调度器停止" << std::endl;
}

void SimpleScheduler::schedule_loop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        LockGuard<SimpleMutex> lock(scheduler_mutex_);

        // 检查是否有就绪进程
        if (ready_queue_.empty()) {
            continue;
        }

        // 如果当前没有运行的进程，调度下一个
        if (current_pid_ == 0) {
            schedule_next();
        }

        // 检查当前进程是否需要抢占
        check_preemption();

        // 清理已完成的进程
        cleanup_finished_processes();
    }
}

void SimpleScheduler::schedule_next() {
    if (ready_queue_.empty()) {
        current_pid_ = 0;
        return;
    }

    uint32_t next_pid = ready_queue_.front();
    ready_queue_.pop();

    const auto it = std::find_if(processes_.begin(), processes_.end(),
                          [next_pid](const Process& p) { return p.pid == next_pid; });

    if (it != processes_.end() && it->state == ProcessState::READY) {
        current_pid_ = next_pid;
        it->state = ProcessState::RUNNING;
        it->remaining_time = it->time_slice;
        it->running = true;
        it->start_time = std::chrono::steady_clock::now();

        // 创建新的线程智能指针
        it->thread = std::make_unique<std::thread>([this, next_pid]() {
            this->run_process(next_pid);
        });

        std::cout << "调度进程: " << it->name << " (PID: " << next_pid << ")" << std::endl;
    }
}

void SimpleScheduler::run_process(uint32_t pid) {
    const auto it = std::find_if(processes_.begin(), processes_.end(),
                          [pid](const Process& p) { return p.pid == pid; });

    if (it == processes_.end()) {
        return;
    }

    try {
        // 执行进程任务
        it->task();

        // 任务完成，标记为完成状态
        LockGuard<SimpleMutex> lock(scheduler_mutex_);
        it->state = ProcessState::TERMINATED;
        it->running = false;

        if (current_pid_ == pid) {
            current_pid_ = 0;
        }

        std::cout << "进程完成: " << it->name << " (PID: " << pid << ")" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "进程异常: " << it->name << " (PID: " << pid << ") - " << e.what() << std::endl;

        it->state = ProcessState::TERMINATED;
        it->running = false;

        if (current_pid_ == pid) {
            current_pid_ = 0;
        }
    }
}

void SimpleScheduler::check_preemption() {
    if (current_pid_ == 0) {
        return;
    }

    const auto it = std::find_if(processes_.begin(), processes_.end(),
                          [this](const Process& p) { return p.pid == current_pid_; });

    if (it == processes_.end() || it->state != ProcessState::RUNNING) {
        return;
    }

    // 检查时间片是否用完
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->start_time);

    if (elapsed.count() >= it->time_slice) {
        // 时间片用完，抢占进程
        std::cout << "时间片用完，抢占进程: " << it->name << " (PID: " << current_pid_ << ")" << std::endl;

        it->state = ProcessState::READY;
        it->running = false;
        ready_queue_.push(current_pid_);
        current_pid_ = 0;

        // 注意：这里不等待线程结束，让任务自然完成
        // 实际的抢占会在任务检查running标志时生效
    }
}

void SimpleScheduler::cleanup_finished_processes() {
    auto it = processes_.begin();
    while (it != processes_.end()) {
        if (it->state == ProcessState::TERMINATED) {
            if (it->thread && it->thread->joinable()) {
                it->thread->join();
            }
            std::cout << "清理进程: " << it->name << " (PID: " << it->pid << ")" << std::endl;
            it = processes_.erase(it);
        } else {
            ++it;
        }
    }
}

void SimpleScheduler::terminate_process(uint32_t pid) {
    LockGuard<SimpleMutex> lock(scheduler_mutex_);

    const auto it = std::find_if(processes_.begin(), processes_.end(),
                          [pid](const Process& p) { return p.pid == pid; });

    if (it != processes_.end()) {
        it->state = ProcessState::TERMINATED;
        it->running = false;

        if (current_pid_ == pid) {
            current_pid_ = 0;
        }

        std::cout << "终止进程: " << it->name << " (PID: " << pid << ")" << std::endl;
    }
}

void SimpleScheduler::print_status() const {
    LockGuard<SimpleMutex> lock(scheduler_mutex_);

    std::cout << "\n=== 调度器状态 ===" << std::endl;
    std::cout << "运行状态: " << (running_ ? "运行中" : "已停止") << std::endl;
    std::cout << "当前进程: " << current_pid_ << std::endl;
    std::cout << "就绪队列长度: " << ready_queue_.size() << std::endl;
    std::cout << "总进程数: " << processes_.size() << std::endl;

    std::cout << "\n进程列表:" << std::endl;
    for (const auto& process : processes_) {
        std::string state_str;
        switch (process.state) {
            case ProcessState::READY: state_str = "就绪"; break;
            case ProcessState::RUNNING: state_str = "运行"; break;
            case ProcessState::WAITING: state_str = "等待"; break;
            case ProcessState::TERMINATED: state_str = "终止"; break;
        }

        std::cout << "  PID: " << process.pid
                  << ", 名称: " << process.name
                  << ", 状态: " << state_str
                  << ", 时间片: " << process.time_slice << "ms" << std::endl;
    }
    std::cout << "==================\n" << std::endl;
}

size_t SimpleScheduler::get_process_count() const {
    LockGuard<SimpleMutex> lock(scheduler_mutex_);
    return processes_.size();
}

size_t SimpleScheduler::get_ready_count() const {
    LockGuard<SimpleMutex> lock(scheduler_mutex_);
    return ready_queue_.size();
}

bool SimpleScheduler::is_running() const {
    LockGuard<SimpleMutex> lock(scheduler_mutex_);
    return running_;
}