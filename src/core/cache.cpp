#include "cache.h"
#include "disk.h"
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <iostream>

CacheManager::CacheManager(VirtualDisk* disk, const size_t page_count, const size_t block_size)
    : disk_(disk), page_count_(page_count), block_size_(block_size) {
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
    int page_index;

    // 1. 加读锁，尝试在缓存中查找
    {
        // ReadWriteLock::ReadGuard lock(rw_lock_);
        page_index = find_page(block_no);
        if (page_index != -1) {
            std::memcpy(buffer, pages_[page_index].data.data(), block_size_);
            return true;
        }
    } // 读锁在这里释放

    // 2. 缓存未命中，需要从磁盘加载，切换为写锁
    // ReadWriteLock::WriteGuard lock(rw_lock_);

    // 3. 再次检查，防止在切换锁的间隙，其他线程已经加载了该页
    page_index = find_page(block_no);
    if (page_index != -1) {
        std::memcpy(buffer, pages_[page_index].data.data(), block_size_);
        return true;
    }

    // 4. 确实不在缓存中，获取一个空闲页（或替换一个页）
    page_index = get_free_page();
    if (page_index == -1) {
        return false; // 没有可用的缓存页
    }

    // 5. 从磁盘读取数据到缓存页
    if (!disk_->read_block(block_no, pages_[page_index].data.data())) {
        return false;
    }

    // 6. 初始化新缓存页
    pages_[page_index].block_no = block_no;
    pages_[page_index].dirty = false;
    pages_[page_index].access_time = time(nullptr);
    block_to_page_[block_no] = page_index;
    fifo_queue_.push(page_index);

    // 7. 将数据复制到输出缓冲区
    std::memcpy(buffer, pages_[page_index].data.data(), block_size_);
    return true;
}

bool CacheManager::write_block(const uint32_t block_no, const void* buffer) {
    // ReadWriteLock::WriteGuard lock(rw_lock_);

    int page_index = find_page(block_no);

    // 如果页面不在缓存中
    if (page_index == -1) {
        page_index = get_free_page();
        if (page_index == -1) {
            return false;
        }

        // **[修复]** 写未命中时，先从磁盘加载原始数据 (Fetch-on-write)
        if (!disk_->read_block(block_no, pages_[page_index].data.data())) {
             // 如果读取失败，可能是一个全新的块，可以忽略错误，或者进行错误处理
        }

        pages_[page_index].block_no = block_no;
        pages_[page_index].access_time = time(nullptr);
        block_to_page_[block_no] = page_index;
        fifo_queue_.push(page_index);
    }

    // 更新页面数据并标记为脏页
    std::memcpy(pages_[page_index].data.data(), buffer, block_size_);
    pages_[page_index].dirty = true;

    return true;
}

void CacheManager::flush_all() {
    // ReadWriteLock::WriteGuard lock(rw_lock_);

    for (size_t i = 0; i < pages_.size(); ++i) {
        if (pages_[i].dirty) {
            write_back_page(i);
        }
    }
}

int CacheManager::find_page(const uint32_t block_no) {
    const auto it = block_to_page_.find(block_no);
    return (it != block_to_page_.end()) ? it->second : -1;
}

int CacheManager::get_free_page() {
    // 查找完全未使用的页面
    for (size_t i = 0; i < pages_.size(); ++i) {
        if (pages_[i].block_no == UINT32_MAX) {
            return i;
        }
    }

    // 执行FIFO置换
    if (!fifo_queue_.empty()) {
        const int victim_index = fifo_queue_.front();
        fifo_queue_.pop();

        // 如果是脏页，先写回磁盘
        if (pages_[victim_index].dirty) {
            write_back_page(victim_index);
        }

        // 从映射中移除旧的块
        block_to_page_.erase(pages_[victim_index].block_no);

        // **[修复]** 重置被替换页的块号，增加健壮性
        pages_[victim_index].block_no = UINT32_MAX;

        return victim_index;
    }

    return -1; // 不应该发生，除非缓存大小为0
}

void CacheManager::write_back_page(const size_t page_index) {
    if (pages_[page_index].block_no != UINT32_MAX && pages_[page_index].dirty) {
        if (!disk_->write_block(pages_[page_index].block_no, pages_[page_index].data.data())) {
            // 在真实系统中，这里需要更复杂的错误处理
            std::cerr << "Fatal: Failed to write back cache page for block " << pages_[page_index].block_no << std::endl;
        }
        pages_[page_index].dirty = false;
    }
}

void CacheManager::print_status() const {
    // ReadWriteLock::ReadGuard lock(rw_lock_);

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

    std::cout << "\n=== 缓存状态 ===" << std::endl;
    std::cout << "总页数: " << page_count_ << " (" << (page_count_ * block_size_ / 1024) << " KiB)" << std::endl;
    std::cout << "已使用页数: " << used_pages << std::endl;
    std::cout << "空闲页数: " << (page_count_ - used_pages) << std::endl;
    std::cout << "脏页数: " << dirty_pages << std::endl;

    if (page_count_ > 0) {
        std::cout << "使用率: " << std::fixed << std::setprecision(2)
                  << (static_cast<double>(used_pages) / page_count_ * 100.0) << "%" << std::endl;
    }
    if (used_pages > 0) {
        std::cout << "脏页率: " << std::fixed << std::setprecision(2)
                  << (static_cast<double>(dirty_pages) / used_pages * 100.0) << "%" << std::endl;
    }

    std::cout << "FIFO队列长度: " << fifo_queue_.size() << std::endl;
    std::cout << std::endl;
}