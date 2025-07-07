#include "sync.h"
#include <iostream>
#include <iomanip>

std::atomic<int> LockManager::lock_count_(0);
std::atomic<int> LockManager::deadlock_count_(0);

void LockManager::print_statistics() {
    std::cout << "\n=== 同步机制统计 ===" << std::endl;
    std::cout << "活跃锁数量: " << lock_count_.load() << std::endl;
    std::cout << "死锁检测次数: " << deadlock_count_.load() << std::endl;
    std::cout << "=====================\n" << std::endl;
}

// 全局同步原语实例
namespace GlobalSync {
    // 文件系统级别的全局锁
    std::mutex filesystem_mutex;

    // 磁盘I/O锁
    std::mutex disk_io_mutex;

    // 日志输出锁
    std::mutex log_mutex;

    // 统计信息锁
    std::mutex stats_mutex;
}