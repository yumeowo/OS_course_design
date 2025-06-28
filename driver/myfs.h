/*
 * MyFS - 简单文件系统头文件
 * 基于Linux VFS接口实现的FAT32风格文件系统
 *
 * 作者: MySimpleFS项目组
 * 版本: 1.0
 * 创建时间: 2025-06-28
 */

#ifndef MYFS_H
#define MYFS_H

/* 添加必要的宏定义 */
#define MYFS_SB(sb)     ((struct myfs_sb_info *)(sb)->s_fs_info)
#define MYFS_I(inode)   (container_of(inode, struct myfs_inode_info, vfs_inode))

/* ==================== 常量定义 ==================== */
#define MYFS_MAGIC              0x1a2b3c4d          /* 文件系统魔数 */
#define MYFS_BLOCK_SIZE         4096                /* 默认块大小(4KB) */
#define MYFS_BLOCK_SIZE_BITS    12                  /* log2(4096) = 12 */
#define MYFS_DEFAULT_BLOCK_COUNT 1024               /* 默认块数量 */
#define MYFS_MAX_NAME_LEN       255                 /* 文件名最大长度 */
#define MYFS_MAX_DIR_DEPTH      2                   /* 最大目录深度 */
#define MYFS_MAX_FILE_SIZE      (1ULL << 32)       /* 最大文件大小(4GB) */

/* inode相关常量 */
#define MYFS_ROOT_INO           1                   /* 根目录inode号 */
#define MYFS_FIRST_USER_INO     2                   /* 第一个用户inode号 */
#define MYFS_INODES_PER_BLOCK   (MYFS_BLOCK_SIZE / sizeof(struct myfs_inode))

/* 缓存相关常量 */
#define MYFS_FIFO_CACHE_SIZE    64                  /* FIFO缓存大小 */

/* 文件类型定义 */
#define MYFS_FT_UNKNOWN         0                   /* 未知类型 */
#define MYFS_FT_REG_FILE        1                   /* 普通文件 */
#define MYFS_FT_DIR             2                   /* 目录 */
#define MYFS_FT_SYMLINK         7                   /* 符号链接 */

/* 文件系统状态 */
#define MYFS_VALID_FS           0                   /* 文件系统正常 */
#define MYFS_ERROR_FS           1                   /* 文件系统错误 */

/* 只在内核空间包含内核头文件 */
#ifdef __KERNEL__
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/blkdev.h>
#include <linux/statfs.h>
#include <linux/mutex.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#else
/* 用户空间需要的头文件 */
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

/* 为用户空间定义对应的类型 */
typedef uint32_t __u32;
typedef uint16_t __u16;
typedef uint8_t __u8;
typedef int32_t __s32;
typedef int16_t __s16;
typedef int8_t __s8;
typedef uint16_t umode_t;

/* 用户空间的字节序类型定义（Linux内核中的 __le32 等类型） */
typedef __u32 __le32;
typedef __u16 __le16;

#endif

/* ==================== 磁盘数据结构 ==================== */

/**
 * 超级块结构体 - 存储在磁盘第0块
 * 包含整个文件系统的宏观元信息
 */
struct myfs_super_block {
    __le32  s_magic;                        /* 魔数 */
    __le32  s_blocks_count;                 /* 总块数 */
    __le32  s_inodes_count;                 /* 总inode数 */
    __le32  s_free_blocks_count;            /* 空闲块数 */
    __le32  s_free_inodes_count;            /* 空闲inode数 */
    __le32  s_first_data_block;             /* 第一个数据块号 */
    __le32  s_block_size;                   /* 块大小 */
    __le32  s_inode_size;                   /* inode大小 */

    /* 位图信息 */
    __le32  s_bitmap_block;                 /* 空闲块位图起始块 */
    __le32  s_bitmap_blocks;                /* 位图占用块数 */

    /* inode表信息 */
    __le32  s_inode_table_block;            /* inode表起始块 */
    __le32  s_inode_table_blocks;           /* inode表占用块数 */

    /* 文件系统状态 */
    __le32  s_state;                        /* 文件系统状态 */
    __le32  s_errors;                       /* 错误处理方式 */

    /* 时间戳 */
    __le32  s_lastcheck;                    /* 最后检查时间 */
    __le32  s_checkinterval;                /* 检查间隔 */
    __le32  s_creator_os;                   /* 创建者操作系统 */
    __le32  s_rev_level;                    /* 版本级别 */

    /* 保留字段 */
    __le32  s_reserved[16];                 /* 保留字段，用于未来扩展 */

} __attribute__((packed));

/**
 * inode结构体 - 存储文件/目录元数据
 * 采用连续存储策略，记录起始块和块数
 */
struct myfs_inode {
    __le16  i_mode;                         /* 文件类型和权限 */
    __le16  i_uid;                          /* 用户ID */
    __le16  i_gid;                          /* 组ID */
    __le16  i_links_count;                  /* 硬链接数 */
    __le32  i_size;                         /* 文件大小(字节) */
    __le32  i_blocks;                       /* 块数量 */
    __le32  i_flags;                        /* 文件标志 */

    /* 连续存储信息 */
    __le32  i_start_block;                  /* 起始数据块号 */
    __le32  i_block_count;                  /* 连续块数量 */

    /* 时间戳 */
    __le32  i_atime;                        /* 访问时间 */
    __le32  i_ctime;                        /* 创建时间 */
    __le32  i_mtime;                        /* 修改时间 */
    __le32  i_dtime;                        /* 删除时间 */

    /* 目录特定信息 */
    __le32  i_parent_ino;                   /* 父目录inode号 */
    __le16  i_dir_level;                    /* 目录层级 */
    __le16  i_reserved1;                    /* 保留字段 */

    /* 保留字段 */
    __le32  i_reserved2[8];                 /* 保留字段 */

} __attribute__((packed));

/**
 * 目录项结构体 - 存储在目录的数据块中
 * 记录目录下的文件和子目录信息
 */
struct myfs_dir_entry {
    __le32  inode;                          /* inode号 */
    __le16  rec_len;                        /* 目录项长度 */
    __u8    name_len;                       /* 文件名长度 */
    __u8    file_type;                      /* 文件类型 */
    char    name[MYFS_MAX_NAME_LEN + 1];    /* 文件名 */
} __attribute__((packed));

/* ==================== 内存数据结构 ==================== */

/* 只在内核空间定义的结构和函数 */
#ifdef __KERNEL__

/**
 * 文件系统私有超级块信息
 * 存储在内存中，用于快速访问元数据
 */
struct myfs_sb_info {
    struct myfs_super_block s_sb;           /* 磁盘超级块副本 */
    __u32   s_blocks_count;                 /* 总块数 */
    __u32   s_inodes_count;                 /* 总inode数 */
    __u32   s_free_blocks_count;            /* 空闲块数 */
    __u32   s_free_inodes_count;            /* 空闲inode数 */
    __u32   s_first_data_block;             /* 第一个数据块 */
    __u32   s_bitmap_block;                 /* 位图起始块 */
    __u32   s_bitmap_blocks;                /* 位图块数 */
    __u32   s_inode_table_block;            /* inode表起始块 */
    __u32   s_inode_table_blocks;           /* inode表块数 */
    __u32   s_state;                        /* 文件系统状态 */

    struct mutex s_lock;                    /* 超级块锁 */
    struct buffer_head *s_sbh;              /* 超级块缓冲区 */
    struct buffer_head *s_bitmap_bh;        /* 位图缓冲区 */
};

/**
 * 内核inode私有信息
 * 扩展标准inode，存储文件系统特有信息
 */
struct myfs_inode_info {
    __u32   i_start_block;                  /* 起始数据块号 */
    __u32   i_block_count;                  /* 数据块数量 */
    __u32   i_parent_ino;                   /* 父目录inode号 */
    __u16   i_dir_level;                    /* 目录层级 */
    __u32   i_flags;                        /* 文件标志 */

    struct mutex i_mutex;                   /* inode锁 */
    struct inode vfs_inode;                 /* VFS inode结构 */
};

/* ==================== 缓存结构定义 ==================== */

/**
 * FIFO缓存项
 * 用于实现页面置换算法
 */
struct myfs_cache_entry {
    __u32   block_no;                       /* 缓存的块号 */
    char    *data;                          /* 缓存数据 */
    int     dirty;                          /* 脏位标记 */
    int     ref_count;                      /* 引用计数 */
    struct list_head list;                  /* 链表节点 */
    struct buffer_head *bh;                 /* 关联的缓冲区头 */
};

/**
 * FIFO缓存管理器
 * 管理固定大小的缓存池
 */
struct myfs_cache {
    struct myfs_cache_entry entries[MYFS_FIFO_CACHE_SIZE];
    struct list_head fifo_list;            /* FIFO队列 */
    struct list_head free_list;            /* 空闲列表 */
    int used_entries;                       /* 已使用条目数 */
    int cache_hits;                         /* 缓存命中次数 */
    int cache_misses;                       /* 缓存未命中次数 */
    spinlock_t lock;                        /* 缓存锁 */
};

/* ==================== 日志结构定义 ==================== */

/**
 * 日志条目类型
 */
enum myfs_log_type {
    MYFS_LOG_CACHE_HIT,                     /* 缓存命中 */
    MYFS_LOG_CACHE_MISS,                    /* 缓存未命中 */
    MYFS_LOG_CACHE_EVICT,                   /* 缓存驱逐 */
    MYFS_LOG_DISK_READ,                     /* 磁盘读取 */
    MYFS_LOG_DISK_WRITE,                    /* 磁盘写入 */
    MYFS_LOG_DIR_UPDATE,                    /* 目录更新 */
    MYFS_LOG_FILE_READ,                     /* 文件读取 */
    MYFS_LOG_FILE_WRITE,                    /* 文件写入 */
    MYFS_LOG_INODE_ALLOC,                   /* inode分配 */
    MYFS_LOG_BLOCK_ALLOC,                   /* 块分配 */
    MYFS_LOG_ERROR,                         /* 错误事件 */
};

/**
 * 日志条目
 */
struct myfs_log_entry {
    struct timespec64 timestamp;            /* 时间戳 */
    enum myfs_log_type type;                /* 日志类型 */
    __u32 block_no;                         /* 相关块号 */
    __u32 inode_no;                         /* 相关inode号 */
    __u32 size;                             /* 数据大小 */
    char message[128];                      /* 日志消息 */
};

/* ==================== 辅助宏定义 ==================== */

/* 从超级块获取私有信息 */
#define MYFS_SB(sb)     ((struct myfs_sb_info *)((sb)->s_fs_info))

/* 从VFS inode获取私有信息 */
#define MYFS_I(inode)   (container_of(inode, struct myfs_inode_info, vfs_inode))

/* 块号相关宏 */
#define MYFS_BLOCK_OFFSET(block)    ((block) * MYFS_BLOCK_SIZE)
#define MYFS_BLOCK_NUMBER(offset)   ((offset) / MYFS_BLOCK_SIZE)

/* 位图操作宏 */
#define MYFS_SET_BIT(nr, addr)      set_bit(nr, addr)
#define MYFS_CLEAR_BIT(nr, addr)    clear_bit(nr, addr)
#define MYFS_TEST_BIT(nr, addr)     test_bit(nr, addr)

/* 目录项相关宏 */
#define MYFS_DIR_ENTRY_SIZE(name_len) \
    (sizeof(struct myfs_dir_entry) - MYFS_MAX_NAME_LEN - 1 + (name_len))

/* ==================== 函数声明 ==================== */

/* myfs.c - 主要文件系统操作 */
extern struct inode *myfs_iget(struct super_block *sb, unsigned long ino);
extern int myfs_fill_super(struct super_block *sb, void *data, int silent);

/* super.c - 超级块操作 */
extern const struct super_operations myfs_super_ops;
extern int myfs_write_super(struct super_block *sb);
extern int myfs_sync_fs(struct super_block *sb, int wait);

/* inode.c - inode操作 */
extern const struct inode_operations myfs_file_inode_operations;
extern const struct inode_operations myfs_dir_inode_operations;
extern struct inode *myfs_new_inode(struct super_block *sb, umode_t mode);
extern int myfs_write_inode(struct inode *inode, struct writeback_control *wbc);
extern void myfs_evict_inode(struct inode *inode);

/* file.c - 文件操作 */
extern const struct file_operations myfs_file_operations;
extern const struct address_space_operations myfs_aops;
extern int myfs_get_block(struct inode *inode, sector_t block,
                         struct buffer_head *bh_result, int create);

/* dir.c - 目录操作 */
extern const struct file_operations myfs_dir_operations;
extern struct dentry *myfs_lookup(struct inode *dir, struct dentry *dentry,
                                 unsigned int flags);
extern int myfs_create(struct inode *dir, struct dentry *dentry,
                      umode_t mode, bool excl);
extern int myfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
extern int myfs_rmdir(struct inode *dir, struct dentry *dentry);
extern int myfs_unlink(struct inode *dir, struct dentry *dentry);

/* cache.c - 缓存管理 */
extern struct myfs_cache *myfs_cache_init(void);
extern void myfs_cache_destroy(struct myfs_cache *cache);
extern struct buffer_head *myfs_cache_get_block(struct myfs_cache *cache,
                                               struct super_block *sb,
                                               __u32 block_no);
extern void myfs_cache_put_block(struct myfs_cache *cache, __u32 block_no);
extern void myfs_cache_sync(struct myfs_cache *cache);

/* log.c - 日志系统 */
extern void myfs_log_init(void);
extern void myfs_log_exit(void);
extern void myfs_log_event(enum myfs_log_type type, __u32 block_no,
                          __u32 inode_no, __u32 size, const char *fmt, ...);

/* 工具函数 */
extern unsigned long myfs_count_free_blocks(struct super_block *sb);
extern unsigned long myfs_count_free_inodes(struct super_block *sb);
extern int myfs_alloc_block(struct super_block *sb);
extern void myfs_free_block(struct super_block *sb, unsigned long block);
extern unsigned long myfs_alloc_inode(struct super_block *sb);
extern void myfs_free_inode(struct super_block *sb, unsigned long ino);

/* 内联函数 */

/**
 * myfs_get_block_bitmap - 获取块位图
 */
static inline unsigned char *myfs_get_block_bitmap(struct super_block *sb)
{
    struct myfs_sb_info *sbi = MYFS_SB(sb);
    return (unsigned char *)sbi->s_bitmap_bh->b_data;
}

/**
 * myfs_mark_buffer_dirty - 标记缓冲区为脏
 */
static inline void myfs_mark_buffer_dirty(struct buffer_head *bh)
{
    mark_buffer_dirty(bh);
}

/**
 * myfs_is_dir_empty - 检查目录是否为空
 */
static inline bool myfs_is_dir_empty(struct inode *inode)
{
    return inode->i_size <= 2 * sizeof(struct myfs_dir_entry);
}

/**
 * myfs_dir_level_valid - 验证目录层级
 */
static inline bool myfs_dir_level_valid(int level)
{
    return level >= 0 && level <= MYFS_MAX_DIR_DEPTH;
}

#endif /* __KERNEL__ */

#endif /* MYFS_H */