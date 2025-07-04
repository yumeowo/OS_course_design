#include "bitmap.h"
#include <iostream>
#include <iomanip>
#include <cstring>  

FreeBitmap::FreeBitmap(const uint32_t total_blocks)
    : total_blocks_(total_blocks),
    free_blocks_(total_blocks),
    mutex_() {  // 显式初始化 mutex
    if (total_blocks == 0) {
        throw std::invalid_argument("总块数不能为零");
    }
    bitmap_.resize((total_blocks + 7) / 8, 0);
}

void FreeBitmap::initialize() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
    for (uint32_t block = 0; block < total_blocks_; ++block) {
        if (is_block_free(block)) {
            return block;
        }
    }
    return UINT32_MAX;  // 没有找到空闲块
}

uint32_t FreeBitmap::find_consecutive_free_blocks(const uint32_t count) const {
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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
    std::lock_guard lock(mutex_);

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
    std::lock_guard lock(mutex_);

    if (block_no >= total_blocks_) {
        return;  // 无效的块号
    }

    set_block_status(block_no, false);
}

void FreeBitmap::free_consecutive_blocks(const uint32_t start_block, const uint32_t count) {
    std::lock_guard lock(mutex_);

    if (start_block >= total_blocks_ || count == 0) {
        return;  // 无效参数
    }

    // 确保不会越界
    const uint32_t end_block = std::min(start_block + count, total_blocks_);

    for (uint32_t block = start_block; block < end_block; ++block) {
        set_block_status(block, false);
    }
}

bool FreeBitmap::is_block_allocated(const uint32_t block_no) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);


    if (block_no >= total_blocks_) {
        return false;  // 无效的块号
    }

    return !is_block_free(block_no);
}

// bitmap.cpp
void FreeBitmap::print_status() {
    // 使用带模板参数的lock_guard（兼容所有C++版本）
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // 获取原子快照
    const uint32_t total = total_blocks_;
    const uint32_t free = free_blocks_;
    const auto& bitmap = bitmap_;

    // 打印信息
    std::cout << "\n=== 空闲盘块表状态 ===" << std::endl;
    std::cout << "总块数: " << total << std::endl;
    std::cout << "空闲块数: " << free << std::endl;
    std::cout << "已使用块数: " << (total - free) << std::endl;

    // 安全计算使用率
    if (total > 0) {
        std::cout << "使用率: " << std::fixed << std::setprecision(2)
            << ((total - free) * 100.0 / total) << "%" << std::endl;
    }
    else {
        std::cout << "使用率: N/A (总块数为0)" << std::endl;
    }

    // 打印位图样本
    const size_t sample_size = std::min<size_t>(8, bitmap.size());
    std::cout << "位图样本（前" << sample_size << "字节）: ";
    for (size_t i = 0; i < sample_size; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(bitmap[i]) << " ";
    }
    std::cout << std::dec << std::endl;
}

bool FreeBitmap::validate() {
    std::lock_guard lock(mutex_);

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
bool FreeBitmap::serialize_to(void* buffer, size_t buffer_size) const {
    const size_t required_size = bitmap_.size();
    if (buffer_size < required_size) {
        return false;
    }
    memcpy(buffer, bitmap_.data(), required_size);
    return true;
}

bool FreeBitmap::deserialize_from(const void* buffer, size_t buffer_size) {
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

void FreeBitmap::mark_block_used(uint32_t block_id) {
    if (block_id >= total_blocks_) return;
    set_block_status(block_id, true);
}