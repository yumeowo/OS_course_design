#include "bitmap.h"
#include <iostream>
#include <iomanip>
#include <cstring>  

FreeBitmap::FreeBitmap(const uint32_t total_blocks)
    : total_blocks_(total_blocks),
    free_blocks_(total_blocks) {
    if (total_blocks == 0) {
        throw std::invalid_argument("总块数不能为零");
    }
    bitmap_.resize((total_blocks + 7) / 8, 0);
}

void FreeBitmap::initialize() {
    ReadWriteLock::ReadGuard guard(rw_lock_);

    // 重置所有位为0（空闲状态）
    std::fill(bitmap_.begin(), bitmap_.end(), 0);
    free_blocks_ = total_blocks_;
}

bool FreeBitmap::is_block_free(const uint32_t block_no) const {
    if (block_no >= total_blocks_) {
        return false;
    }

    const uint32_t byte_index = block_no / 8;
    const uint32_t bit_index = block_no % 8;

    // 如果对应位为0，表示空闲
    return !(bitmap_[byte_index] & (1 << bit_index));
}

void FreeBitmap::set_block_status(const uint32_t block_no, const bool allocated) {
    if (block_no >= total_blocks_) {
        return;
    }

    const uint32_t byte_index = block_no / 8;
    const uint32_t bit_index = block_no % 8;
    const uint8_t mask = 1 << bit_index;

    const bool was_free = !(bitmap_[byte_index] & mask);

    if (allocated) {
        // 设置位为1（已分配）
        bitmap_[byte_index] |= mask;
        if (was_free) {
            free_blocks_--;
        }
    }
    else {
        // 清除位为0（空闲）
        bitmap_[byte_index] &= ~mask;
        if (!was_free) {
            free_blocks_++;
        }
    }
}

uint32_t FreeBitmap::find_first_free_block() const {
    ReadWriteLock::WriteGuard guard(rw_lock_);

    for (uint32_t block = 0; block < total_blocks_; ++block) {
        if (is_block_free(block)) {
            return block;
        }
    }
    return UINT32_MAX;  // 没有找到空闲块
}

uint32_t FreeBitmap::find_consecutive_free_blocks(const uint32_t count) const {
    ReadWriteLock::WriteGuard guard(rw_lock_);

    if (count == 0 || count > free_blocks_) {
        return UINT32_MAX;
    }

    for (uint32_t start = 0; start <= total_blocks_ - count; ++start) {
        bool found = true;

        // 检查从start开始的count个连续块是否都空闲
        for (uint32_t i = 0; i < count; ++i) {
            if (!is_block_free(start + i)) {
                found = false;
                break;
            }
        }

        if (found) {
            return start;
        }
    }

    return UINT32_MAX;  // 没有找到足够的连续空闲块
}

bool FreeBitmap::allocate_block(uint32_t& block_no) {
    ReadWriteLock::WriteGuard guard(rw_lock_);

    if (free_blocks_ == 0) {
        return false;  // 没有空闲块
    }

    const uint32_t free_block = find_first_free_block();
    if (free_block == UINT32_MAX) {
        std::cerr << "Error: Failed to find free block" << std::endl;
        return false;  // 没有找到空闲块（理论上不应该发生）
    }

    set_block_status(free_block, true);
    block_no = free_block;
    return true;
}

bool FreeBitmap::allocate_consecutive_blocks(const uint32_t count, uint32_t& start_block) {
    ReadWriteLock::WriteGuard guard(rw_lock_);

    if (count == 0) {
        return false;
    }

    if (count > free_blocks_) {
        return false;  // 空闲块数不足
    }

    const uint32_t start = find_consecutive_free_blocks(count);
    if (start == UINT32_MAX) {
        return false;  // 没有找到足够的连续空闲块
    }

    // 分配所有找到的连续块
    for (uint32_t i = 0; i < count; ++i) {
        set_block_status(start + i, true);
    }

    start_block = start;
    return true;
}

void FreeBitmap::free_block(const uint32_t block_no) {
    ReadWriteLock::WriteGuard guard(rw_lock_);

    if (block_no >= total_blocks_) {
        return;  // 无效的块号
    }

    set_block_status(block_no, false);
}

void FreeBitmap::free_consecutive_blocks(const uint32_t start_block, const uint32_t count) {
    ReadWriteLock::WriteGuard guard(rw_lock_);

    if (start_block >= total_blocks_ || count == 0) {
        return;  // 无效的起始块号或计数
    }

    // 确保不会越界
    const uint32_t end_block = std::min(start_block + count, total_blocks_);

    for (uint32_t block = start_block; block < end_block; ++block) {
        set_block_status(block, false);
    }
}

bool FreeBitmap::is_block_allocated(const uint32_t block_no) const
{
    ReadWriteLock::WriteGuard guard(rw_lock_);

    if (block_no >= total_blocks_) {
        return false;  // 无效的块号
    }

    return !is_block_free(block_no);
}

void FreeBitmap::print_status() const
{
    // 创建局部快照，避免长时间持有锁
    std::vector<uint8_t> sample;
    uint32_t total, free;
    size_t sample_size;
    {
        ReadWriteLock::WriteGuard guard(rw_lock_);

        // 创建局部快照，避免长时间持有锁
        total = total_blocks_;
        free = free_blocks_;
        sample_size = std::min<size_t>(8, bitmap_.size());
        sample.assign(bitmap_.begin(), bitmap_.begin() + sample_size);
    } // guard在这里被销毁，避免在持有锁时进行I/O操作

    // 打印信息
    std::cout << "\n=== 空闲盘块表状态 ===" << std::endl;
    std::cout << "总块数: " << total << std::endl;
    std::cout << "空闲块数: " << free << std::endl;
    std::cout << "已使用块数: " << (total - free) << std::endl;

    if (total > 0) {
        std::cout << "使用率: " << std::fixed << std::setprecision(2)
            << ((total - free) * 100.0 / total) << "%" << std::endl;
    }
    else {
        std::cout << "使用率: N/A (总块数为0)" << std::endl;
    }

    // 打印位图样本
    std::cout << "位图样本（前" << sample_size << "字节）: ";
    for (size_t i = 0; i < sample_size; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(sample[i]) << " ";
    }
    std::cout << std::dec << std::endl;
}

bool FreeBitmap::validate() const
{
    ReadWriteLock::WriteGuard guard(rw_lock_);

    // 重新计算空闲块数，验证内部状态是否一致
    uint32_t calculated_free_blocks = 0;
    for (uint32_t block = 0; block < total_blocks_; ++block) {
        if (is_block_free(block)) {
            calculated_free_blocks++;
        }
    }

    const bool is_valid = (calculated_free_blocks == free_blocks_);

    if (!is_valid) {
        std::cerr << "位图验证失败: 计算的空闲块数(" << calculated_free_blocks
            << ") != 记录的空闲块数(" << free_blocks_ << ")" << std::endl;
    }

    return is_valid;
}
bool FreeBitmap::serialize_to(void* buffer, const size_t buffer_size) const {
    ReadWriteLock::WriteGuard guard(rw_lock_);

    if (buffer == nullptr || buffer_size == 0) {
        return false;  // 无效的缓冲区
    }
    const size_t required_size = bitmap_.size();
    if (buffer_size < required_size) {
        return false; // 缓冲区大小不足
    }
    memcpy(buffer, bitmap_.data(), required_size);
    return true;
}

bool FreeBitmap::deserialize_from(const void* buffer, const size_t buffer_size) {
    ReadWriteLock::WriteGuard guard(rw_lock_);

    const size_t required_size = bitmap_.size();
    if (buffer_size < required_size) {
        return false;
    }
    memcpy(bitmap_.data(), buffer, required_size);

    // 重新计算空闲块数
    free_blocks_ = 0;
    for (uint32_t block = 0; block < total_blocks_; ++block) {
        if (is_block_free(block)) {
            free_blocks_++;
        }
    }
    return true;
}

void FreeBitmap::mark_block_used(const uint32_t block_id) {
    ReadWriteLock::WriteGuard guard(rw_lock_);

    if (block_id >= total_blocks_) return;
    set_block_status(block_id, true);
}

bool FreeBitmap::initialize(VirtualDisk* disk) {
    ReadWriteLock::WriteGuard guard(rw_lock_);

    // 获取磁盘总块数
    total_blocks_ = disk->get_total_blocks();
    if (total_blocks_ == 0) {
        return false;
    }

    // 重置位图
    bitmap_.resize((total_blocks_ + 7) / 8, 0);
    free_blocks_ = total_blocks_;

    // 写入位图到磁盘第一个块
    return save(disk);
}

bool FreeBitmap::load(VirtualDisk* disk) {
    ReadWriteLock::WriteGuard guard(rw_lock_);

    // 获取磁盘总块数
    total_blocks_ = disk->get_total_blocks();
    if (total_blocks_ == 0) {
        return false;
    }

    // 调整位图大小
    const size_t bitmap_size = (total_blocks_ + 7) / 8;
    bitmap_.resize(bitmap_size);

    // 从磁盘第一个块读取位图
    if (!disk->read_block(0, bitmap_.data())) {
        return false;
    }

    // 重新计算空闲块数
    free_blocks_ = 0;
    for (uint32_t block = 0; block < total_blocks_; ++block) {
        if (is_block_free(block)) {
            free_blocks_++;
        }
    }

    return true;
}

bool FreeBitmap::save(VirtualDisk* disk) const {
    ReadWriteLock::ReadGuard guard(rw_lock_);

    // 将位图写入磁盘第一个块
    return disk->write_block(0, bitmap_.data());
}