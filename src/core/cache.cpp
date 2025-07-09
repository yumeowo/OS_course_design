#include "cache.h"
#include "disk.h"
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <iostream>

CacheManager::CacheManager(VirtualDisk* disk, const size_t page_count, const size_t block_size)
    : disk_(disk), page_count_(page_count), block_size_(block_size) {
    // 初始化缓存页
    pages_.resize(page_count_);
    for (auto& page : pages_) {
        page.block_no = UINT32_MAX;
        page.dirty = false;
        page.access_time = 0;
        page.data.resize(block_size_);
    }
}

CacheManager::~CacheManager() {
    flush_all();
}

bool CacheManager::read_block(const uint32_t block_no, void* buffer) {
    // 使用读锁，允许多个读操作并发
    ReadWriteLock::ReadGuard lock(rw_lock_);
    LockManager::register_lock();
    
    int page_index = find_page(block_no);
    
    // 如果页面不在缓存中，需要升级为写锁
    if (page_index == -1) {
        lock.~ReadGuard();  // 释放读锁
        ReadWriteLock::WriteGuard write_lock(rw_lock_);  // 获取写锁

        // 重新检查，避免竞态条件
        page_index = find_page(block_no);
        if (page_index == -1) {
            page_index = get_free_page();
            if (page_index == -1) {
                LockManager::unregister_lock();
                return false;
            }

            // 从磁盘读取数据
            if (!disk_->read_block(block_no, pages_[page_index].data.data())) {
                LockManager::unregister_lock();
                return false;
            }

            // 初始化新页面
            pages_[page_index].block_no = block_no;
            pages_[page_index].dirty = false;
            pages_[page_index].access_time = time(nullptr);

            // 更新映射
            block_to_page_[block_no] = page_index;
            fifo_queue_.push(page_index);
        }
    }

    // 复制数据到缓冲区
    std::memcpy(buffer, pages_[page_index].data.data(), block_size_);

    LockManager::unregister_lock();
    return true;
}

bool CacheManager::write_block(const uint32_t block_no, const void* buffer) {
    // 写操作需要写锁
    ReadWriteLock::WriteGuard lock(rw_lock_);
    LockManager::register_lock();

    int page_index = find_page(block_no);

    // 如果页面不在缓存中
    if (page_index == -1) {
        page_index = get_free_page();
        if (page_index == -1) {
            LockManager::unregister_lock();
            return false;
        }

        // 初始化新页面
        pages_[page_index].block_no = block_no;
        pages_[page_index].access_time = time(nullptr);

        // 更新映射
        block_to_page_[block_no] = page_index;
        fifo_queue_.push(page_index);
    }

    // 更新页面数据
    std::memcpy(pages_[page_index].data.data(), buffer, block_size_);
    pages_[page_index].dirty = true;

    LockManager::unregister_lock();
    return true;
}

void CacheManager::flush_all() {
    ReadWriteLock::WriteGuard lock(rw_lock_);
    LockManager::register_lock();

    // 写回所有脏页
    for (size_t i = 0; i < pages_.size(); ++i) {
        if (pages_[i].dirty) {
            write_back_page(i);
        }
    }

    LockManager::unregister_lock();
}

int CacheManager::find_page(const uint32_t block_no) {
    // 对于快速查找操作，使用自旋锁
    SpinLock spin_lock;
    spin_lock.lock();

    const auto it = block_to_page_.find(block_no);
    const int result = (it != block_to_page_.end()) ? it->second : -1;

    spin_lock.unlock();
    return result;
}

int CacheManager::get_free_page() {
    // 如果有空闲页面
    for (size_t i = 0; i < pages_.size(); ++i) {
        if (pages_[i].block_no == UINT32_MAX) {
            return i;
        }
    }
    
    // 没有空闲页面，使用FIFO策略替换
    if (!fifo_queue_.empty()) {
        const int victim_index = fifo_queue_.front();
        fifo_queue_.pop();
        
        // 如果是脏页，先写回
        if (pages_[victim_index].dirty) {
            write_back_page(victim_index);
        }
        
        // 从映射中移除旧块
        block_to_page_.erase(pages_[victim_index].block_no);
        
        return victim_index;
    }
    
    return -1;
}

void CacheManager::write_back_page(const size_t page_index) {
    if (pages_[page_index].block_no != UINT32_MAX) {
        // 将页面数据写回虚拟磁盘
        if (!disk_->write_block(pages_[page_index].block_no, pages_[page_index].data.data())) {
            // 写入失败的错误处理
            throw std::runtime_error("Failed to write back cache page");
        }
        pages_[page_index].dirty = false;
    }
}

void CacheManager::print_status() const {
    // 加读锁进行访问
    ReadWriteLock::ReadGuard lock(rw_lock_);

    // 统计脏页数量和缓存命中情况
    uint32_t dirty_pages = 0;
    uint32_t used_pages = 0;

    for (const auto& page : pages_) {
        if (page.block_no != UINT32_MAX) {
            used_pages++;
            if (page.dirty) {
                dirty_pages++;
            }
        }
    }

    // 打印缓存状态信息
    std::cout << "\n=== 缓存状态 ===" << std::endl;
    std::cout << "总页数: " << page_count_ << std::endl;
    std::cout << "已使用页数: " << used_pages << std::endl;
    std::cout << "空闲页数: " << (page_count_ - used_pages) << std::endl;
    std::cout << "脏页数: " << dirty_pages << std::endl;

    if (used_pages > 0) {
        std::cout << "使用率: " << std::fixed << std::setprecision(2)
                  << (used_pages * 100.0 / page_count_) << "%" << std::endl;
        std::cout << "脏页率: " << std::fixed << std::setprecision(2)
                  << (dirty_pages * 100.0 / used_pages) << "%" << std::endl;
    }

    // 打印FIFO队列状态
    std::cout << "FIFO队列长度: " << fifo_queue_.size() << std::endl;

    // 打印前几个缓存页的状态样本
    const size_t sample_size = std::min<size_t>(4, pages_.size());
    std::cout << "\n缓存页样本(前" << sample_size << "页):" << std::endl;
    for (size_t i = 0; i < sample_size; ++i) {
        const auto& page = pages_[i];
        if (page.block_no != UINT32_MAX) {
            std::cout << "[" << i << "] 块号:" << page.block_no
                     << " 脏:" << (page.dirty ? "是" : "否")
                     << " 访问时间:" << page.access_time << std::endl;
        } else {
            std::cout << "[" << i << "] 空闲" << std::endl;
        }
    }
    std::cout << std::endl;
}