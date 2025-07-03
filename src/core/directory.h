#ifndef DIRECTORY_H
#define DIRECTORY_H

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "inode.h"

// 目录项结构
struct DirectoryEntry {
    uint32_t inode_id;      // 对应的inode ID
    char name[64];          // 目录项名称
    uint8_t type;          // 类型(文件/目录)
};

class Directory {
private:
    static constexpr uint8_t TYPE_FILE = 1;
    static constexpr uint8_t TYPE_DIR = 2;
    static constexpr size_t MAX_ENTRIES = 256;  // 每个目录最大项数

    uint32_t dir_inode_id_;        // 当前目录的inode ID
    std::vector<DirectoryEntry> entries_;  // 目录项列表
    std::mutex mutex_;             // 目录操作互斥锁

public:
    // 构造函数
    explicit Directory(uint32_t dir_inode_id);

    // 添加目录项
    bool add_entry(const std::string& name, uint32_t inode_id, uint8_t type);

    // 删除目录项
    bool remove_entry(const std::string& name);

    // 查找目录项
    bool find_entry(const std::string& name, DirectoryEntry& entry);

    // 获取所有目录项
    std::vector<DirectoryEntry> list_entries() const;

    // 检查是否为空目录
    bool is_empty() const;

    // 获取目录项数量
    size_t get_entry_count() const;

    // 获取目录的inode ID
    uint32_t get_inode_id() const;

    // 序列化目录内容(用于存储)
    std::vector<uint8_t> serialize() const;

    // 反序列化目录内容(用于加载)
    bool deserialize(const std::vector<uint8_t>& data);

    // 验证目录结构完整性
    bool validate() const;
};

#endif //DIRECTORY_H