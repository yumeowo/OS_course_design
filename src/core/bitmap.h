#ifndef BITMAP_H
#define BITMAP_H

#include <vector>
#include <mutex>
#include <memory> 

/**
 * 空闲盘块表 - 使用位图管理磁盘空间
 * 支持单块和连续块的分配，采用位图方式管理空闲状态
 */
class FreeBitmap
{
    std::vector<uint8_t> bitmap_; // 位图数组，每个bit表示一个块的状态
    uint32_t total_blocks_; // 总块数
    uint32_t free_blocks_; // 空闲块数
    mutable std::recursive_mutex mutex_;  // 

    /**
     * 检查指定块是否空闲
     * @param block_no 块号
     * @return true如果空闲，false如果已分配
     */
    bool is_block_free(uint32_t block_no) const;

    /**
     * 设置块的状态
     * @param block_no 块号
     * @param allocated true表示分配，false表示释放
     */
    void set_block_status(uint32_t block_no, bool allocated);

    /**
     * 查找第一个空闲块
     * @return 空闲块号，如果没有返回UINT32_MAX
     */
    uint32_t find_first_free_block() const;

    /**
     * 查找指定数量的连续空闲块
     * @param count 需要的连续块数
     * @return 起始块号，如果找不到返回UINT32_MAX
     */
    uint32_t find_consecutive_free_blocks(uint32_t count) const;

public:

    FreeBitmap(const FreeBitmap&) = delete;
    FreeBitmap& operator=(const FreeBitmap&) = delete;
    FreeBitmap(FreeBitmap&&) = delete;
    FreeBitmap& operator=(FreeBitmap&&) = delete;

    /**
     * 构造函数
     * @param total_blocks 总块数
     */
    explicit FreeBitmap(uint32_t total_blocks);

    /**
     * 析构函数
     */
    ~FreeBitmap() = default;

    /**
     * 初始化位图，所有块都标记为空闲
     */
    void initialize();

    /**
     * 分配一个空闲块
     * @param block_no 输出参数，返回分配的块号
     * @return true如果分配成功，false如果没有空闲块
     */
    bool allocate_block(uint32_t& block_no);

    /**
     * 分配指定数量的连续空闲块
     * @param count 需要分配的连续块数
     * @param start_block 输出参数，返回起始块号
     * @return true如果分配成功，false如果没有足够的连续空闲块
     */
    bool allocate_consecutive_blocks(uint32_t count, uint32_t& start_block);

    /**
     * 释放一个块
     * @param block_no 要释放的块号
     */
    void free_block(uint32_t block_no);

    /**
     * 释放连续的多个块
     * @param start_block 起始块号
     * @param count 要释放的块数
     */
    void free_consecutive_blocks(uint32_t start_block, uint32_t count);

    /**
     * 获取总块数
     * @return 总块数
     */
    uint32_t get_total_blocks() const { return total_blocks_; }

    /**
     * 获取空闲块数
     * @return 空闲块数
     */
    uint32_t get_free_blocks() const
    {
        std::lock_guard lock(mutex_);
        return free_blocks_;
    }

    /**
     * 获取已使用块数
     * @return 已使用块数
     */
    uint32_t get_used_blocks() const
    {
        std::lock_guard lock(mutex_);
        return total_blocks_ - free_blocks_;
    }

    /**
     * 获取磁盘使用率
     * @return 使用率百分比 (0.0 - 1.0)
     */
    double get_usage_ratio() const
    {
        std::lock_guard lock(mutex_);
        if (total_blocks_ == 0) {
            return 0.0; // 避免除以零
        }

        return static_cast<double>(total_blocks_ - free_blocks_) / total_blocks_;
    }

    /**
     * 检查指定块是否已分配
     * @param block_no 块号
     * @return true如果已分配，false如果空闲或块号无效
     */
    bool is_block_allocated(uint32_t block_no) const;

    /**
     * 打印位图状态信息（用于调试）
     */
    void print_status() const;

    /**
     * 验证位图数据完整性
     * @return true如果数据一致，false如果有错误
     */
    bool validate() const;
    bool serialize_to(void* buffer, size_t buffer_size) const;
    bool deserialize_from(const void* buffer, size_t buffer_size);

    bool is_mutex_valid() const {
        // 尝试加锁测试（仅用于调试）
        bool valid = true;
        try {
            std::lock_guard<std::recursive_mutex> lock(mutex_);

        }
        catch (...) {
            valid = false;
        }
        return valid;
    }

    void mark_block_used(uint32_t block_id);
};

#endif //BITMAP_H
