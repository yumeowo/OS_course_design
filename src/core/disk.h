//
// Created by 28396 on 2025/6/29.
//

#ifndef DISK_H
#define DISK_H

#define DISK_SIZE 256000000     // 磁盘大小 256MB
#define BLOCK_SIZE 4096         // 磁盘块大小 4KiB

#include <cstdint>
#include <fstream>
#include <string>

class VirtualDisk {
    std::string disk_file_; // 磁盘文件名
    size_t disk_size_ = DISK_SIZE; // 磁盘大小256MiB
    size_t block_size_ = BLOCK_SIZE; // 块大小4KiB
    std::fstream file_stream_; // 文件流
    uint32_t total_blocks_; // 总块数

public:
    VirtualDisk(std::string filename, const size_t size_mb, const size_t block_size = BLOCK_SIZE)
        : disk_file_(std::move(filename)), disk_size_(size_mb * 1024 * 1024), block_size_(block_size) {
        total_blocks_ = static_cast<uint32_t>(disk_size_ / block_size_);
    }

    bool create(const std::string& filename, size_t size_mb);
    bool read_block(uint32_t block_no, void* buffer);
    bool write_block(uint32_t block_no, const void* buffer);
    bool copy_blocks(uint32_t src_block, uint32_t dst_block, uint32_t count);
};

#endif //DISK_H
