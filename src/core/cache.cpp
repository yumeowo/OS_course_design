#include "cache.h"
#include "disk.h"
#include <cstring>
#include <algorithm>

CacheManager::CacheManager(VirtualDisk& disk, const size_t page_count, const size_t block_size)
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
    std::lock_guard<std::mutex> lock(mutex_);
    
    int page_index = find_page(block_no);
    
    // 如果页面不在缓存中
    if (page_index == -1) {
        page_index = get_free_page();
        if (page_index == -1) return false;
        
        // 初始化新页面
        pages_[page_index].block_no = block_no;
        pages_[page_index].dirty = false;
        pages_[page_index].access_time = time(nullptr);
        
        // 更新映射
        block_to_page_[block_no] = page_index;
        fifo_queue_.push(page_index);
    }

    // 复制数据到缓冲区
    std::memcpy(buffer, pages_[page_index].data.data(), block_size_);
    return true;
}

bool CacheManager::write_block(const uint32_t block_no, const void* buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    int page_index = find_page(block_no);
    
    // 如果页面不在缓存中
    if (page_index == -1) {
        page_index = get_free_page();
        if (page_index == -1) return false;
        
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
    return true;
}

void CacheManager::flush_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 写回所有脏页
    for (size_t i = 0; i < pages_.size(); ++i) {
        if (pages_[i].dirty) {
            write_back_page(i);
        }
    }
}

int CacheManager::find_page(const uint32_t block_no) {
    const auto it = block_to_page_.find(block_no);
    if (it != block_to_page_.end()) {
        return it->second;
    }
    return -1;
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
        if (!disk_.write_block(pages_[page_index].block_no, pages_[page_index].data.data())) {
            // 写入失败的错误处理
            throw std::runtime_error("Failed to write back cache page");
        }
        pages_[page_index].dirty = false;
    }
}