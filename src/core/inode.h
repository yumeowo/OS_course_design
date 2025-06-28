#ifndef INODE_H
#define INODE_H
#include <cstdint>
#include <ctime>

struct INode {
    uint32_t id;                    // 节点ID
    uint8_t type;                   // 类型（文件/目录）
    uint32_t size;                  // 文件大小
    uint32_t start_block;           // 起始块号
    uint32_t block_count;           // 占用块数
    uint32_t parent_id;             // 父目录ID
    time_t create_time;             // 创建时间
    time_t modify_time;             // 修改时间
    char name[64];                  // 文件名
};

#endif //INODE_H
