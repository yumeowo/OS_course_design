#include "disk.h"
#include <iostream>
#include <vector>

/**
 * 创建虚拟磁盘文件
 * @param filename 磁盘文件名
 * @param size_mb 磁盘大小（MB）
 * @return 创建成功返回true，失败返回false
 */
bool VirtualDisk::create(const std::string& filename, const size_t size_mb) {
    disk_file_ = filename;
    disk_size_ = size_mb * 1024 * 1024; // 将MiB转换为字节

    // 以二进制模式创建文件
    file_stream_.open(disk_file_, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file_stream_.is_open()) {
        std::cerr << "Error: Failed to create disk file: " << disk_file_ << std::endl;
        return false;
    }

    // 创建指定大小的磁盘文件，用0填充
    const std::vector<char> buffer(block_size_, 0);
    const uint32_t total_blocks = disk_size_ / block_size_;

    for (uint32_t i = 0; i < total_blocks; ++i) {
        file_stream_.write(buffer.data(), block_size_);
        if (file_stream_.fail()) {
            std::cerr << "Error: Failed to write block " << i << " during disk creation" << std::endl;
            file_stream_.close();
            return false;
        }
    }

    file_stream_.close();

    // 重新以读写模式打开文件
    file_stream_.open(disk_file_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_stream_.is_open()) {
        std::cerr << "Error: Failed to reopen disk file: " << disk_file_ << std::endl;
        return false;
    }

    std::cout << "Virtual disk created successfully: " << disk_file_
              << " (Size: " << size_mb << "MB, Blocks: " << total_blocks << ")" << std::endl;

    return true;
}

/**
 * 读取磁盘块
 * @param block_no 块号
 * @param buffer 读取缓冲区
 * @return 读取成功返回true，失败返回false
 */
bool VirtualDisk::read_block(const uint32_t block_no, void* buffer) {
    if (!buffer) {
        std::cerr << "Error: Invalid buffer pointer" << std::endl;
        return false;
    }

    if (!file_stream_.is_open()) {
        std::cerr << "Error: Disk file is not open" << std::endl;
        return false;
    }

    const uint32_t total_blocks = total_blocks_;
    if (block_no >= total_blocks) {
        std::cerr << "Error: Block number " << block_no << " exceeds disk capacity ("
                  << total_blocks << " blocks)" << std::endl;
        return false;
    }

    // 计算块在文件中的偏移位置
    const std::streampos offset = static_cast<std::streampos>(block_no) * block_size_;

    // 定位到指定位置
    file_stream_.seekg(offset);
    if (file_stream_.fail()) {
        std::cerr << "Error: Failed to seek to block " << block_no << std::endl;
        return false;
    }

    // 读取数据
    file_stream_.read(static_cast<char*>(buffer), block_size_);
    if (file_stream_.fail() || file_stream_.gcount() != static_cast<std::streamsize>(block_size_)) {
        std::cerr << "Error: Failed to read block " << block_no
                  << " (Read " << file_stream_.gcount() << " bytes, expected " << block_size_ << ")" << std::endl;
        return false;
    }

    return true;
}

/**
 * 写入磁盘块
 * @param block_no 块号
 * @param buffer 写入数据缓冲区
 * @return 写入成功返回true，失败返回false
 */
bool VirtualDisk::write_block(const uint32_t block_no, const void* buffer) {
    if (!buffer) {
        std::cerr << "Error: Invalid buffer pointer" << std::endl;
        return false;
    }

    if (!file_stream_.is_open()) {
        std::cerr << "Error: Disk file is not open" << std::endl;
        return false;
    }

    const uint32_t total_blocks = total_blocks_;
    if (block_no >= total_blocks) {
        std::cerr << "Error: Block number " << block_no << " exceeds disk capacity ("
                  << total_blocks << " blocks)" << std::endl;
        return false;
    }

    // 计算块在文件中的偏移位置
    const std::streampos offset = static_cast<std::streampos>(block_no) * block_size_;

    // 定位到指定位置
    file_stream_.seekp(offset);
    if (file_stream_.fail()) {
        std::cerr << "Error: Failed to seek to block " << block_no << std::endl;
        return false;
    }

    // 写入数据
    file_stream_.write(static_cast<const char*>(buffer), block_size_);
    if (file_stream_.fail()) {
        std::cerr << "Error: Failed to write block " << block_no << std::endl;
        return false;
    }

    // 强制刷新缓冲区到磁盘
    file_stream_.flush();

    return true;
}

bool VirtualDisk::copy_blocks(uint32_t src_block, uint32_t dst_block, uint32_t count) {
    std::vector<uint8_t> buffer(BLOCK_SIZE);
    for (uint32_t i = 0; i < count; ++i) {
        if (!read_block(src_block + i, buffer.data())) {
            return false;
        }
        if (!write_block(dst_block + i, buffer.data())) {
            return false;
        }
    }
    return true;
}