#include "inode.h"
#include "disk.h"
#include "bitmap.h"
#include <cstring>
#include <stdexcept>
#include <cmath>

// 每个INode结构体大小（字节）
constexpr uint32_t INODE_SIZE = sizeof(INode);

// 每块可存储的INode数量
constexpr uint32_t INODES_PER_BLOCK = BLOCK_SIZE / INODE_SIZE;

INodeManager::INodeManager(VirtualDisk* disk, FreeBitmap* bitmap) 
    : disk_(disk), bitmap_(bitmap) {
    inode_table_start_ = 1;
    inode_used_.resize(MAX_FILES, false);
    inode_count_ = 0;

    // 可选：启动时扫描 INODE 表恢复使用状态（用于 mount）
    for (uint32_t i = 0; i < MAX_FILES; ++i) {
        INode node;
        if (read_inode(i, &node)) {
            if (validate_inode(&node)) {
                inode_used_[i] = true;
                inode_count_++;
            }
        }
    }
}

int32_t INodeManager::create_inode(uint32_t parent_id, uint8_t type,
                                   const std::string& name, uint32_t size) {
    if (inode_count_ >= MAX_FILES) {
        return -1; // 无可用 inode
    }

    // 寻找未使用的 inode 槽位
    int32_t inode_id = -1;
    for (uint32_t i = 0; i < MAX_FILES; ++i) {
        if (!inode_used_[i]) {
            inode_id = i;
            break;
        }
    }
    if (inode_id == -1) return -1;

    // 分配连续块
    uint32_t block_count = calculate_blocks_needed(size);
    uint32_t start_block = 0;

    if (type == FS_FILE) {
        if (!bitmap_->allocate_consecutive_blocks(block_count, start_block)) {
            return -2;
        }
    } else {
        if (!bitmap_->allocate_consecutive_blocks(1, start_block)) {
            return -2;
        }
        block_count = 1;
    }

    INode new_node{
        .id = static_cast<uint32_t>(inode_id),
        .type = type,
        .size = size,
        .start_block = start_block,
        .block_count = block_count,
        .parent_id = parent_id,
        .create_time = time(nullptr),
        .modify_time = time(nullptr)
    };
    strncpy(new_node.name, name.c_str(), sizeof(new_node.name) - 1);
    new_node.name[sizeof(new_node.name) - 1] = '\0';

    if (!write_inode(new_node.id, &new_node)) {
        bitmap_->free_consecutive_blocks(start_block, block_count);
        return -3;
    }

    inode_used_[inode_id] = true;
    inode_count_++;
    return inode_id;
}

bool INodeManager::read_inode(uint32_t inode_id, INode* node) {
    if (inode_id >= MAX_FILES) return false;

    uint32_t block_index = inode_id / INODES_PER_BLOCK + inode_table_start_;
    uint32_t block_offset = (inode_id % INODES_PER_BLOCK) * INODE_SIZE;

    std::vector<uint8_t> block(BLOCK_SIZE);
    if (!disk_->read_block(block_index, block.data())) return false;

    memcpy(node, block.data() + block_offset, INODE_SIZE);
    return true;
}

bool INodeManager::write_inode(uint32_t inode_id, const INode* node) {
    if (inode_id >= MAX_FILES) return false;

    uint32_t block_index = inode_id / INODES_PER_BLOCK + inode_table_start_;
    uint32_t block_offset = (inode_id % INODES_PER_BLOCK) * INODE_SIZE;

    std::vector<uint8_t> block(BLOCK_SIZE);
    if (!disk_->read_block(block_index, block.data())) return false;

    memcpy(block.data() + block_offset, node, INODE_SIZE);
    return disk_->write_block(block_index, block.data());
}

bool INodeManager::delete_inode(uint32_t inode_id) {
    if (inode_id >= MAX_FILES || !inode_used_[inode_id]) return false;

    INode node;
    if (!read_inode(inode_id, &node)) return false;

    if (node.type == FS_FILE) {
        bitmap_->free_consecutive_blocks(node.start_block, node.block_count);
    }

    node.type = 0xFF;
    node.size = 0;
    node.block_count = 0;
    node.start_block = 0;
    node.name[0] = '\0';

    if (!write_inode(inode_id, &node)) return false;

    inode_used_[inode_id] = false;
    inode_count_--;
    return true;
}

bool INodeManager::resize_inode(uint32_t inode_id, uint32_t new_size) {
    INode node;
    if (!read_inode(inode_id, &node) || node.type != FS_FILE) return false;

    uint32_t new_blocks = calculate_blocks_needed(new_size);
    uint32_t old_blocks = node.block_count;

    if (new_blocks == old_blocks) {
        node.size = new_size;
        node.modify_time = time(nullptr);
        return write_inode(inode_id, &node);
    }

    if (new_blocks > old_blocks) {
        uint32_t additional = new_blocks - old_blocks;
        bool contiguous = true;

        // 检查后续 block 是否空闲（确保从 node.start_block + old_blocks 开始有足够空位）
        for (uint32_t i = 0; i < additional; ++i) {
            if (bitmap_->is_block_used(node.start_block + old_blocks + i)) {
                contiguous = false;
                break;
            }
        }

        if (contiguous) {
            // 直接手动标记新块为已使用
            for (uint32_t i = 0; i < additional; ++i) {
                bitmap_->mark_block_used(node.start_block + old_blocks + i); // 你需要在 FreeBitmap 里实现这个函数
            }

            node.block_count = new_blocks;
            node.size = new_size;
            node.modify_time = time(nullptr);
            return write_inode(inode_id, &node);
        }
    }

    // 不连续，重新分配块
    uint32_t new_start;
    if (!bitmap_->allocate_consecutive_blocks(new_blocks, new_start)) {
        return false;
    }

    if (!disk_->copy_blocks(node.start_block, new_start, old_blocks)) {
        bitmap_->free_consecutive_blocks(new_start, new_blocks);
        return false;
    }

    bitmap_->free_consecutive_blocks(node.start_block, old_blocks);
    node.start_block = new_start;
    node.block_count = new_blocks;
    node.size = new_size;
    node.modify_time = time(nullptr);

    return write_inode(inode_id, &node);
}

uint32_t INodeManager::calculate_blocks_needed(uint32_t size) const {
    return (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

bool INodeManager::validate_inode(const INode* node) const {
    return node->id < MAX_FILES && 
           (node->type == FS_FILE || node->type == FS_DIRECTORY) &&
           node->block_count > 0;
}

int32_t INodeManager::find_inode(uint32_t parent_id, const std::string& name) {
    for (uint32_t i = 0; i < MAX_FILES; ++i) {
        if (!inode_used_[i]) continue;
        INode node;
        if (read_inode(i, &node) &&
            node.parent_id == parent_id &&
            strcmp(node.name, name.c_str()) == 0) {
            return i;
        }
    }
    return -1;
}

uint32_t INodeManager::get_total_inodes() const {
    return inode_count_;
}
