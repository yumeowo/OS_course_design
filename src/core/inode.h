#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "disk.h"
#include "bitmap.h"
#include "directory.h"
#include "../process/sync.h"

// INode 类型定义
#define FS_FILE 0
#define FS_DIRECTORY 1

// 根目录 ID
#define ROOT_INODE_ID 1

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

// 文件信息结构
struct FileInfo {
    std::string name;
    std::string path;
    bool is_directory;
    size_t size;
    time_t create_time;
    time_t modify_time;
    uint32_t block_count;
    uint32_t start_block;
    uint32_t inode_id;

    FileInfo() : is_directory(false), size(0), create_time(0), modify_time(0),
                 block_count(0), start_block(0), inode_id(0) {}
};

class INodeManager {
public:
    INodeManager(VirtualDisk* disk, FreeBitmap* bitmap);
    ~INodeManager();

    // 初始化和格式化
    static bool initialize();
    bool create_root_directory();

    // 核心 inode 操作
    int32_t create_inode(uint32_t parent_id, uint8_t type,
                         const std::string& name, uint32_t size);
    bool read_inode(uint32_t inode_id, INode* node) const;
    bool write_inode(uint32_t inode_id, const INode* node) const;
    bool delete_inode(uint32_t inode_id);
    int32_t find_inode(uint32_t parent_id, const std::string& name) const;

    // 辅助功能
    bool resize_inode(uint32_t inode_id, uint32_t new_size) const;
    uint32_t get_total_inodes() const;
    static uint32_t get_root_inode_id() { return ROOT_INODE_ID; }

    // 文件系统操作
    bool create_file(const std::string& path, const std::string& content = "");
    bool create_directory(const std::string& parent_path, const std::string& name);
    bool delete_file(const std::string& path);
    bool delete_directory(const std::string& path);

    // 文件读写操作
    bool read_file(const std::string& path, std::string& content) const;
    bool write_file(const std::string& path, const std::string& content) const;
    bool read_file_block(const std::string& path, uint32_t block_index, std::string& content) const;
    bool write_file_block(const std::string& path, uint32_t block_index, const std::string& content) const;

    // 目录操作
    std::vector<FileInfo> list_directory(const std::string& path) const;
    FileInfo get_file_info(const std::string& path) const;
    bool file_exists(const std::string& path) const;
    bool directory_exists(const std::string& path) const;

    // 路径解析
    int32_t resolve_path(const std::string& path) const;
    std::string get_absolute_path(uint32_t inode_id) const;

private:
    // 添加同步原语
    mutable ReadWriteLock inode_lock_;           // 保护整个inode表
    mutable std::vector<std::unique_ptr<SpinLock>> inode_locks_;  // 每个inode的细粒度锁
    mutable SimpleMutex allocation_mutex_;       // 保护分配操作

    // 磁盘和资源管理
    VirtualDisk* disk_;             // 虚拟磁盘指针
    FreeBitmap* bitmap_;            // 空闲块位图
    uint32_t inode_table_start_ = 1;    // INode表起始块号
    uint32_t inode_count_ = 0;      // 当前INode数量
    uint32_t max_inodes_ = MAX_FILES;           // 最大inode数量

    // 目录缓存
    mutable std::unordered_map<uint32_t, std::shared_ptr<Directory>> directory_cache_;
    mutable SimpleMutex cache_mutex_;

    // 私有方法
    static uint32_t calculate_blocks_needed(uint32_t size);
    static bool validate_inode(const INode* node);
    std::vector<bool> inode_used_;

    // 目录相关的私有方法
    bool load_directory_content(uint32_t dir_id, Directory& dir) const;
    bool save_directory_content(uint32_t dir_id, const Directory& dir) const;
    std::shared_ptr<Directory> get_directory(uint32_t dir_id) const;
    void cache_directory(uint32_t dir_id, std::unique_ptr<Directory> dir) const;
    void remove_from_cache(uint32_t dir_id) const;

    // 路径解析辅助方法
    static std::vector<std::string> split_path(const std::string& path);
    static std::string normalize_path(const std::string& path);
    static bool is_valid_filename(const std::string& name);

    // 文件读写辅助方法
    bool read_file_data(uint32_t inode_id, std::string& content) const;
    bool write_file_data(uint32_t inode_id, const std::string& content) const;
    bool read_file_block_data(uint32_t inode_id, uint32_t block_index, std::string& content) const;
    bool write_file_block_data(uint32_t inode_id, uint32_t block_index, const std::string& content) const;

    // 目录操作辅助方法
    bool add_directory_entry(uint32_t dir_id, const std::string& name, uint32_t child_id, uint8_t type) const;
    bool remove_directory_entry(uint32_t dir_id, const std::string& name) const;
    bool is_directory_empty(uint32_t dir_id) const;

    // 递归删除目录
    bool delete_directory_recursive(uint32_t dir_id);
};