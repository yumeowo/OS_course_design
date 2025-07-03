#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <unordered_map>
#include <cstdint>
#include <ctime>

#include "disk.h"

struct CachePage {
    uint32_t block_no;              // 缓存的块号
    bool dirty;                     // 脏标记
    time_t access_time;             // 访问时间（用于FIFO）
    std::vector<uint8_t> data;      // 缓存数据
};

class CacheManager {
public:
    explicit CacheManager(VirtualDisk& disk, size_t page_count = 16, size_t block_size = 4096);
    ~CacheManager();

    bool read_block(uint32_t block_no, void* buffer);
    bool write_block(uint32_t block_no, const void* buffer);
    void flush_all();

private:
    VirtualDisk& disk_;
    std::vector<CachePage> pages_;
    std::queue<uint32_t> fifo_queue_;
    std::unordered_map<uint32_t, uint32_t> block_to_page_;
    std::mutex mutex_;
    
    const size_t page_count_;
    const size_t block_size_;

    // 内部辅助方法
    int find_page(uint32_t block_no);
    int get_free_page();
    void write_back_page(size_t page_index);
};