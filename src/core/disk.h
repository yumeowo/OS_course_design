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
    size_t disk_size_ = DISK_SIZE; // 磁盘大小256MB
    size_t block_size_ = BLOCK_SIZE; // 块大小4KiB
    std::fstream file_stream_; // 文件流

public:
    bool create(const std::string& filename, size_t size_mb);
    bool read_block(uint32_t block_no, void* buffer);
    bool write_block(uint32_t block_no, const void* buffer);
    uint32_t get_total_blocks() const;
};

#endif //DISK_H
