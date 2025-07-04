#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// INode 类型定义
#define FS_FILE 0
#define FS_DIRECTORY 1

// INode 结构体定义
struct INode {
    uint32_t id;                    // 节点ID
    uint8_t type;                   // 类型（文件/目录）
    uint32_t size;                  // 文件大小（字节）
    uint32_t start_block;           // 起始块号（连续存储）
    uint32_t block_count;           // 占用块数
    uint32_t parent_id;             // 父目录ID
    time_t create_time;             // 创建时间
    time_t modify_time;             // 修改时间
    char name[64];                  // 文件名/目录名
};

class INodeManager {
public:
    INodeManager(VirtualDisk* disk, FreeBitmap* bitmap);

    // 核心接口
    int32_t create_inode(uint32_t parent_id, uint8_t type,
                         const std::string& name, uint32_t size);
    bool read_inode(uint32_t inode_id, INode* node);
    bool write_inode(uint32_t inode_id, const INode* node);
    bool delete_inode(uint32_t inode_id);
    int32_t find_inode(uint32_t parent_id, const std::string& name);

    // 辅助功能
    bool resize_inode(uint32_t inode_id, uint32_t new_size);
    uint32_t get_total_inodes() const;

private:
    // 私有方法
    uint32_t calculate_blocks_needed(uint32_t size) const;
    bool validate_inode(const INode* node) const;
    std::vector<bool> inode_used_;

    // 磁盘和资源管理
    VirtualDisk* disk_;             // 虚拟磁盘指针
    FreeBitmap* bitmap_;            // 空闲块位图
    uint32_t inode_table_start_;    // INode表起始块号
    uint32_t inode_count_ = 0;      // 当前INode数量
};