#ifndef FS_H
#define FS_H

#include <linux/types.h>
#include <linux/fs.h>

// --- 常量定义 ---
#define MYFS_MAGIC_NUMBER 0x1a2b3c4d // 一个独特的魔数，用于识别我们的文件系统
#define MYFS_DEFAULT_BLOCK_SIZE 4096 // 默认块大小为 4 KB
#define MYFS_DEFAULT_BLOCK_COUNT 1024 // 默认块数量为 1024
#define MYFS_FILENAME_MAX_LEN 255    // 文件名的最大长度
#define MYFS_MAX_DIR_DEPTH 2                 // 支持二级目录的最大深度
#define MYFS_FIFO_CACHE_SIZE 64

// --- 磁盘数据结构定义 ---
// 这些结构体描述了数据在物理磁盘（或磁盘镜像文件）上的存储格式。
// 使用空闲盘块位图管理空闲空间，并为文件分配连续的数据块。

/**
 * @brief 超级块结构体 (Superblock)\n
 * 存储在磁盘的第0块，包含了整个文件系统的宏观元信息。
 */
struct fs_super_block {
    __u32 magic_number;                       // 魔数，用于挂载时验证文件系统类型
    __u32 total_blocks;                       // 文件系统总块数
    __u32 block_size;                         // 每个块的大小（字节）

    // 空闲盘块位图信息
    __u32 free_block_bitmap_start_block;      // 空闲盘块位图的起始块号
    __u32 free_block_bitmap_total_blocks;     // 空闲盘块位图占用的总块数

    // Inode区信息
    __u32 inode_start_block;                  // Inode区的起始块号
    __u32 inode_total_blocks;                 // Inode区占用的总块数
    __u32 max_inodes;                         // 最大Inode数量

    // 数据区的起始块号
    __u32 data_start_block;

    // 根目录的inode编号
    __u32 root_dir_inode;

    // 文件系统状态
    __u32 state;                              // 文件系统状态 (0=clean, 1=error)
    __u32 reserved[16];                       // 保留字段，用于未来扩展
};

/**
 * @brief 文件或目录的 Inode 结构体\n
 * Inode 包含了文件的元数据。
 */
struct fs_inode {
    __u16 i_mode;                            // 文件类型和权限
    __u16 i_uid;                             // 用户ID
    __u16 i_gid;                             // 组ID
    __u16 i_n_links;                          // 硬链接计数

    __u64 i_size;                            // 文件大小（字节）

    // 连续存储信息
    __u32 i_start_block;                     // 文件内容占用的第一个数据块的块号
    __u32 i_blocks_count;                    // 文件内容占用的连续数据块的总数

    // 时间戳
    __u32 i_atime;                           // 访问时间
    __u32 i_mtime;                           // 修改时间
    __u32 i_ctime;                           // 创建时间

    // 目录特定信息
    __u32 i_parent_inode;                    // 父目录的inode编号（支持二级目录导航）
    __u16 i_dir_level;                       // 目录层级 (0=根目录, 1=一级目录, 2=二级目录)

    __u32 reserved[8];                       // 保留字段
};


/**
 * @brief 目录项结构体 (Directory Entry)\n
 * 存储在目录文件的数据块中，用于记录该目录下的文件和子目录。
 */
struct fs_dir_entry {
    __u32 inode_num;                         // 该文件对应的 Inode 编号
    __u16 rec_len;                           // 本目录项的长度
    __u8 name_len;                           // 文件名长度
    __u8 file_type;                          // 文件类型 (1=文件, 2=目录)
    char name[MYFS_FILENAME_MAX_LEN + 1];    // 文件名（变长）
};

#endif //FS_H
