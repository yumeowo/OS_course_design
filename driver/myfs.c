/*
 * MyFS - 简单文件系统驱动程序
 * 基于Linux VFS接口实现的FAT32风格文件系统
 *
 * 作者: MySimpleFS项目组
 * 版本: 1.0
 * 创建时间: 2025-06-28
 */

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
#include <linux/namei.h>
#include <linux/vmalloc.h>
#include "myfs.h"

/* 模块信息 */
MODULE_AUTHOR("MySimpleFS Team");
MODULE_DESCRIPTION("Simple File System Driver based on FAT32-like structure with FIFO cache");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");

/* 全局变量 */
static struct kmem_cache *myfs_inode_cachep = NULL;
static struct myfs_cache *global_cache = NULL;

/* ==================== 内部函数声明 ==================== */

/* 挂载与超级块操作 */
static struct dentry *myfs_mount(struct file_system_type *fs_type,
                                int flags, const char *dev_name, void *data);
static void myfs_kill_sb(struct super_block *sb);
static struct inode *myfs_alloc_inode(struct super_block *sb);
static void myfs_destroy_inode(struct inode *inode);
static int myfs_statfs(struct dentry *dentry, struct kstatfs *buf);
static void myfs_put_super(struct super_block *sb);
static int myfs_sync_fs(struct super_block *sb, int wait);
static int myfs_write_inode(struct inode *inode, struct writeback_control *wbc);
static void myfs_evict_inode(struct inode *inode);

/* 内部工具函数 */
static int myfs_read_super_block(struct super_block *sb);
static int myfs_setup_bitmap(struct super_block *sb);
static struct myfs_inode *myfs_read_inode_raw(struct super_block *sb,
                                             unsigned long ino);
static int myfs_write_inode_raw(struct super_block *sb, unsigned long ino,
                               struct myfs_inode *raw_inode);

/* ==================== 超级块操作表 ==================== */

const struct super_operations myfs_super_ops = {
    .alloc_inode    = myfs_alloc_inode,
    .destroy_inode  = myfs_destroy_inode,
    .write_inode    = myfs_write_inode,
    .evict_inode    = myfs_evict_inode,
    .put_super      = myfs_put_super,
    .sync_fs        = myfs_sync_fs,
    .statfs         = myfs_statfs,
};

/* 文件系统类型定义 */
static struct file_system_type myfs_fs_type = {
    .owner      = THIS_MODULE,
    .name       = "myfs",
    .mount      = myfs_mount,
    .kill_sb    = myfs_kill_sb,
    .fs_flags   = FS_REQUIRES_DEV,
};

/* ==================== 文件系统挂载实现 ==================== */

/**
 * myfs_mount - 挂载文件系统
 */
static struct dentry *myfs_mount(struct file_system_type *fs_type,
                                int flags, const char *dev_name, void *data)
{
    printk(KERN_INFO "MyFS: Mounting filesystem on device %s\n", dev_name);
    myfs_log_event(MYFS_LOG_DISK_READ, 0, 0, 0, "Mount filesystem on %s", dev_name);
    return mount_bdev(fs_type, flags, dev_name, data, myfs_fill_super);
}

/**
 * myfs_kill_sb - 卸载文件系统
 */
static void myfs_kill_sb(struct super_block *sb)
{
    struct myfs_sb_info *sbi = MYFS_SB(sb);

    if (sbi) {
        printk(KERN_INFO "MyFS: Unmounting filesystem (blocks: %u, inodes: %u)\n",
               sbi->s_blocks_count, sbi->s_inodes_count);
        myfs_log_event(MYFS_LOG_DISK_WRITE, 0, 0, 0, "Unmount filesystem");
    }

    kill_block_super(sb);
}

/**
 * myfs_fill_super - 填充超级块
 */
int myfs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct myfs_sb_info *sbi;
    struct buffer_head *bh;
    struct myfs_super_block *disk_sb;
    struct inode *root_inode;
    int ret = 0;

    printk(KERN_INFO "MyFS: Filling super block\n");

    /* 设置块大小 */
    if (!sb_set_blocksize(sb, MYFS_BLOCK_SIZE)) {
        if (!silent)
            printk(KERN_ERR "MyFS: Unable to set blocksize to %d\n",
                   MYFS_BLOCK_SIZE);
        return -EINVAL;
    }

    /* 分配超级块私有信息 */
    sbi = kzalloc(sizeof(struct myfs_sb_info), GFP_KERNEL);
    if (!sbi) {
        printk(KERN_ERR "MyFS: Unable to allocate super block info\n");
        return -ENOMEM;
    }

    sb->s_fs_info = sbi;
    mutex_init(&sbi->s_lock);

    /* 读取磁盘超级块 */
    ret = myfs_read_super_block(sb);
    if (ret) {
        if (!silent)
            printk(KERN_ERR "MyFS: Cannot read super block\n");
        goto failed_read_sb;
    }

    /* 设置超级块参数 */
    sb->s_magic = MYFS_MAGIC;
    sb->s_op = &myfs_super_ops;
    sb->s_maxbytes = MYFS_MAX_FILE_SIZE;
    sb->s_blocksize = MYFS_BLOCK_SIZE;
    sb->s_blocksize_bits = MYFS_BLOCK_SIZE_BITS;

    /* 设置位图 */
    ret = myfs_setup_bitmap(sb);
    if (ret) {
        if (!silent)
            printk(KERN_ERR "MyFS: Cannot setup bitmap\n");
        goto failed_bitmap;
    }

    /* 获取根目录inode */
    root_inode = myfs_iget(sb, MYFS_ROOT_INO);
    if (IS_ERR(root_inode)) {
        if (!silent)
            printk(KERN_ERR "MyFS: Cannot get root inode\n");
        ret = PTR_ERR(root_inode);
        goto failed_root;
    }

    /* 创建根目录项 */
    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) {
        if (!silent)
            printk(KERN_ERR "MyFS: Cannot allocate root dentry\n");
        ret = -ENOMEM;
        goto failed_root;
    }

    /* 初始化全局缓存 */
    if (!global_cache) {
        global_cache = myfs_cache_init();
        if (!global_cache) {
            printk(KERN_WARNING "MyFS: Cannot initialize cache, continuing without cache\n");
        }
    }

    printk(KERN_INFO "MyFS: Filesystem mounted successfully\n");
    myfs_log_event(MYFS_LOG_DISK_READ, 0, 0, 0, "Filesystem mounted successfully");
    return 0;

failed_root:
    if (sbi->s_bitmap_bh)
        brelse(sbi->s_bitmap_bh);
failed_bitmap:
    if (sbi->s_sbh)
        brelse(sbi->s_sbh);
failed_read_sb:
    kfree(sbi);
    sb->s_fs_info = NULL;
    return ret;
}

/**
 * myfs_read_super_block - 读取并验证磁盘超级块
 */
static int myfs_read_super_block(struct super_block *sb)
{
    struct myfs_sb_info *sbi = MYFS_SB(sb);
    struct buffer_head *bh;
    struct myfs_super_block *disk_sb;

    /* 读取第0块（超级块） */
    bh = sb_bread(sb, 0);
    if (!bh) {
        printk(KERN_ERR "MyFS: Cannot read super block\n");
        return -EIO;
    }

    disk_sb = (struct myfs_super_block *)bh->b_data;

    /* 验证魔数 */
    if (le32_to_cpu(disk_sb->s_magic) != MYFS_MAGIC) {
        printk(KERN_ERR "MyFS: Invalid magic number: 0x%x (expected 0x%x)\n",
               le32_to_cpu(disk_sb->s_magic), MYFS_MAGIC);
        brelse(bh);
        return -EINVAL;
    }

    /* 复制超级块数据到内存 */
    memcpy(&sbi->s_sb, disk_sb, sizeof(struct myfs_super_block));
    sbi->s_blocks_count = le32_to_cpu(disk_sb->s_blocks_count);
    sbi->s_inodes_count = le32_to_cpu(disk_sb->s_inodes_count);
    sbi->s_free_blocks_count = le32_to_cpu(disk_sb->s_free_blocks_count);
    sbi->s_free_inodes_count = le32_to_cpu(disk_sb->s_free_inodes_count);
    sbi->s_first_data_block = le32_to_cpu(disk_sb->s_first_data_block);
    sbi->s_bitmap_block = le32_to_cpu(disk_sb->s_bitmap_block);
    sbi->s_bitmap_blocks = le32_to_cpu(disk_sb->s_bitmap_blocks);
    sbi->s_inode_table_block = le32_to_cpu(disk_sb->s_inode_table_block);
    sbi->s_inode_table_blocks = le32_to_cpu(disk_sb->s_inode_table_blocks);
    sbi->s_state = le32_to_cpu(disk_sb->s_state);

    sbi->s_sbh = bh;

    printk(KERN_INFO "MyFS: Super block loaded (blocks: %u, inodes: %u, state: %u)\n",
           sbi->s_blocks_count, sbi->s_inodes_count, sbi->s_state);

    return 0;
}

/**
 * myfs_setup_bitmap - 设置空闲块位图
 */
static int myfs_setup_bitmap(struct super_block *sb)
{
    struct myfs_sb_info *sbi = MYFS_SB(sb);
    struct buffer_head *bh;

    /* 读取位图块 */
    bh = sb_bread(sb, sbi->s_bitmap_block);
    if (!bh) {
        printk(KERN_ERR "MyFS: Cannot read bitmap block %u\n",
               sbi->s_bitmap_block);
        return -EIO;
    }

    sbi->s_bitmap_bh = bh;

    printk(KERN_INFO "MyFS: Bitmap loaded from block %u\n", sbi->s_bitmap_block);
    return 0;
}

/* ==================== inode操作实现 ==================== */

/**
 * myfs_iget - 获取inode
 */
struct inode *myfs_iget(struct super_block *sb, unsigned long ino)
{
    struct inode *inode;
    struct myfs_inode_info *mi;
    struct myfs_inode *raw_inode;
    int ret;

    inode = iget_locked(sb, ino);
    if (!inode)
        return ERR_PTR(-ENOMEM);

    if (!(inode->i_state & I_NEW))
        return inode;

    mi = MYFS_I(inode);

    /* 读取磁盘inode */
    raw_inode = myfs_read_inode_raw(sb, ino);
    if (IS_ERR(raw_inode)) {
        iget_failed(inode);
        return ERR_CAST(raw_inode);
    }

    /* 填充VFS inode */
    inode->i_mode = le16_to_cpu(raw_inode->i_mode);
    i_uid_write(inode, le16_to_cpu(raw_inode->i_uid));
    i_gid_write(inode, le16_to_cpu(raw_inode->i_gid));
    set_nlink(inode, le16_to_cpu(raw_inode->i_links_count));
    inode->i_size = le32_to_cpu(raw_inode->i_size);
    inode->i_blocks = le32_to_cpu(raw_inode->i_blocks);

    inode->i_atime.tv_sec = le32_to_cpu(raw_inode->i_atime);
    inode->i_ctime.tv_sec = le32_to_cpu(raw_inode->i_ctime);
    inode->i_mtime.tv_sec = le32_to_cpu(raw_inode->i_mtime);
    inode->i_atime.tv_nsec = 0;
    inode->i_ctime.tv_nsec = 0;
    inode->i_mtime.tv_nsec = 0;

    /* 填充私有信息 */
    mi->i_start_block = le32_to_cpu(raw_inode->i_start_block);
    mi->i_block_count = le32_to_cpu(raw_inode->i_block_count);
    mi->i_parent_ino = le32_to_cpu(raw_inode->i_parent_ino);
    mi->i_dir_level = le16_to_cpu(raw_inode->i_dir_level);
    mi->i_flags = le32_to_cpu(raw_inode->i_flags);

    mutex_init(&mi->i_mutex);

    /* 设置操作表 */
    if (S_ISREG(inode->i_mode)) {
        inode->i_op = &myfs_file_inode_operations;
        inode->i_fop = &myfs_file_operations;
        inode->i_mapping->a_ops = &myfs_aops;
    } else if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &myfs_dir_inode_operations;
        inode->i_fop = &myfs_dir_operations;
    } else {
        /* 其他类型暂不支持 */
        printk(KERN_WARNING "MyFS: Unsupported inode type: %o\n", inode->i_mode);
    }

    kfree(raw_inode);
    unlock_new_inode(inode);

    myfs_log_event(MYFS_LOG_DISK_READ, 0, ino, 0, "Read inode %lu", ino);
    return inode;
}

/**
 * myfs_read_inode_raw - 从磁盘读取原始inode
 */
static struct myfs_inode *myfs_read_inode_raw(struct super_block *sb,
                                             unsigned long ino)
{
    struct myfs_sb_info *sbi = MYFS_SB(sb);
    struct buffer_head *bh;
    struct myfs_inode *raw_inode;
    unsigned long block_no;
    unsigned long offset;

    if (ino < MYFS_ROOT_INO || ino > sbi->s_inodes_count) {
        printk(KERN_ERR "MyFS: Invalid inode number: %lu\n", ino);
        return ERR_PTR(-EINVAL);
    }

    /* 计算inode在磁盘上的位置 */
    block_no = sbi->s_inode_table_block +
               ((ino - 1) * sizeof(struct myfs_inode)) / MYFS_BLOCK_SIZE;
    offset = ((ino - 1) * sizeof(struct myfs_inode)) % MYFS_BLOCK_SIZE;

    bh = sb_bread(sb, block_no);
    if (!bh) {
        printk(KERN_ERR "MyFS: Cannot read inode table block %lu\n", block_no);
        return ERR_PTR(-EIO);
    }

    raw_inode = kmalloc(sizeof(struct myfs_inode), GFP_KERNEL);
    if (!raw_inode) {
        brelse(bh);
        return ERR_PTR(-ENOMEM);
    }

    memcpy(raw_inode, bh->b_data + offset, sizeof(struct myfs_inode));
    brelse(bh);

    return raw_inode;
}

/**
 * myfs_alloc_inode - 分配新的VFS inode
 */
static struct inode *myfs_alloc_inode(struct super_block *sb)
{
    struct myfs_inode_info *mi;

    mi = kmem_cache_alloc(myfs_inode_cachep, GFP_KERNEL);
    if (!mi)
        return NULL;

    /* 初始化私有字段 */
    mi->i_start_block = 0;
    mi->i_block_count = 0;
    mi->i_parent_ino = 0;
    mi->i_dir_level = 0;
    mi->i_flags = 0;
    mutex_init(&mi->i_mutex);

    return &mi->vfs_inode;
}

/**
 * myfs_destroy_inode - 销毁inode
 */
static void myfs_destroy_inode(struct inode *inode)
{
    struct myfs_inode_info *mi = MYFS_I(inode);
    kmem_cache_free(myfs_inode_cachep, mi);
}

/**
 * myfs_write_inode - 写入inode到磁盘
 */
static int myfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
    struct myfs_inode_info *mi = MYFS_I(inode);
    struct myfs_inode raw_inode;
    int ret;

    /* 填充磁盘inode结构 */
    raw_inode.i_mode = cpu_to_le16(inode->i_mode);
    raw_inode.i_uid = cpu_to_le16(i_uid_read(inode));
    raw_inode.i_gid = cpu_to_le16(i_gid_read(inode));
    raw_inode.i_links_count = cpu_to_le16(inode->i_nlink);
    raw_inode.i_size = cpu_to_le32(inode->i_size);
    raw_inode.i_blocks = cpu_to_le32(inode->i_blocks);
    raw_inode.i_flags = cpu_to_le32(mi->i_flags);

    raw_inode.i_start_block = cpu_to_le32(mi->i_start_block);
    raw_inode.i_block_count = cpu_to_le32(mi->i_block_count);
    raw_inode.i_parent_ino = cpu_to_le32(mi->i_parent_ino);
    raw_inode.i_dir_level = cpu_to_le16(mi->i_dir_level);

    raw_inode.i_atime = cpu_to_le32(inode->i_atime.tv_sec);
    raw_inode.i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
    raw_inode.i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
    raw_inode.i_dtime = 0;

    ret = myfs_write_inode_raw(inode->i_sb, inode->i_ino, &raw_inode);
    if (ret == 0) {
        myfs_log_event(MYFS_LOG_DISK_WRITE, 0, inode->i_ino,
                      sizeof(struct myfs_inode), "Write inode %lu", inode->i_ino);
    }

    return ret;
}

/**
 * myfs_write_inode_raw - 写入原始inode到磁盘
 */
static int myfs_write_inode_raw(struct super_block *sb, unsigned long ino,
                               struct myfs_inode *raw_inode)
{
    struct myfs_sb_info *sbi = MYFS_SB(sb);
    struct buffer_head *bh;
    unsigned long block_no;
    unsigned long offset;

    /* 计算inode在磁盘上的位置 */
    block_no = sbi->s_inode_table_block +
               ((ino - 1) * sizeof(struct myfs_inode)) / MYFS_BLOCK_SIZE;
    offset = ((ino - 1) * sizeof(struct myfs_inode)) % MYFS_BLOCK_SIZE;

    bh = sb_bread(sb, block_no);
    if (!bh) {
        printk(KERN_ERR "MyFS: Cannot read inode table block %lu\n", block_no);
        return -EIO;
    }

    memcpy(bh->b_data + offset, raw_inode, sizeof(struct myfs_inode));
    mark_buffer_dirty(bh);

    if (sync_dirty_buffer(bh)) {
        brelse(bh);
        return -EIO;
    }

    brelse(bh);
    return 0;
}

/**
 * myfs_evict_inode - 驱逐inode
 */
static void myfs_evict_inode(struct inode *inode)
{
    truncate_inode_pages_final(&inode->i_data);
    clear_inode(inode);

    myfs_log_event(MYFS_LOG_DISK_WRITE, 0, inode->i_ino, 0,
                  "Evict inode %lu", inode->i_ino);
}

/* ==================== 超级块操作实现 ==================== */

/**
 * myfs_put_super - 释放超级块
 */
static void myfs_put_super(struct super_block *sb)
{
    struct myfs_sb_info *sbi = MYFS_SB(sb);

    if (sbi) {
        if (sbi->s_sbh)
            brelse(sbi->s_sbh);
        if (sbi->s_bitmap_bh)
            brelse(sbi->s_bitmap_bh);
        kfree(sbi);
        sb->s_fs_info = NULL;
    }

    printk(KERN_INFO "MyFS: Super block released\n");
}

/**
 * myfs_sync_fs - 同步文件系统
 */
static int myfs_sync_fs(struct super_block *sb, int wait)
{
    struct myfs_sb_info *sbi = MYFS_SB(sb);
    int ret = 0;

    mutex_lock(&sbi->s_lock);

    if (sbi->s_sbh) {
        mark_buffer_dirty(sbi->s_sbh);
        if (wait) {
            ret = sync_dirty_buffer(sbi->s_sbh);
        }
    }

    if (sbi->s_bitmap_bh) {
        mark_buffer_dirty(sbi->s_bitmap_bh);
        if (wait && ret == 0) {
            ret = sync_dirty_buffer(sbi->s_bitmap_bh);
        }
    }

    /* 同步缓存 */
    if (global_cache) {
        myfs_cache_sync(global_cache);
    }

    mutex_unlock(&sbi->s_lock);

    myfs_log_event(MYFS_LOG_DISK_WRITE, 0, 0, 0,
                  "Sync filesystem (wait=%d)", wait);

    return ret;
}

/**
 * myfs_statfs - 获取文件系统统计信息
 */
static int myfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
    struct super_block *sb = dentry->d_sb;
    struct myfs_sb_info *sbi = MYFS_SB(sb);

    buf->f_type = MYFS_MAGIC;
    buf->f_bsize = MYFS_BLOCK_SIZE;
    buf->f_blocks = sbi->s_blocks_count;
    buf->f_bfree = sbi->s_free_blocks_count;
    buf->f_bavail = sbi->s_free_blocks_count;
    buf->f_files = sbi->s_inodes_count;
    buf->f_ffree = sbi->s_free_inodes_count;
    buf->f_namelen = MYFS_MAX_NAME_LEN;

    return 0;
}

/* ==================== 初始化和清理函数 ==================== */

/**
 * myfs_init_inodecache - 初始化inode缓存
 */
static int myfs_init_inodecache(void)
{
    myfs_inode_cachep = kmem_cache_create("myfs_inode_cache",
                                         sizeof(struct myfs_inode_info),
                                         0, SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
                                         NULL);
    if (!myfs_inode_cachep)
        return -ENOMEM;
    return 0;
}

/**
 * myfs_destroy_inodecache - 销毁inode缓存
 */
static void myfs_destroy_inodecache(void)
{
    kmem_cache_destroy(myfs_inode_cachep);
}

/**
 * myfs_init - 模块初始化
 */
static int __init myfs_init(void)
{
    int ret;

    printk(KERN_INFO "MyFS: Loading filesystem module\n");

    /* 初始化日志系统 */
    myfs_log_init();

    /* 初始化inode缓存 */
    ret = myfs_init_inodecache();
    if (ret) {
        printk(KERN_ERR "MyFS: Failed to initialize inode cache\n");
        goto failed_inodecache;
    }

    /* 注册文件系统 */
    ret = register_filesystem(&myfs_fs_type);
    if (ret) {
        printk(KERN_ERR "MyFS: Failed to register filesystem\n");
        goto failed_register;
    }

    printk(KERN_INFO "MyFS: Filesystem module loaded successfully\n");
    return 0;

failed_register:
    myfs_destroy_inodecache();
failed_inodecache:
    myfs_log_exit();
    return ret;
}

/**
 * myfs_exit - 模块清理
 */
static void __exit myfs_exit(void)
{
    printk(KERN_INFO "MyFS: Unloading filesystem module\n");

    /* 销毁全局缓存 */
    if (global_cache) {
        myfs_cache_destroy(global_cache);
        global_cache = NULL;
    }

    /* 注销文件系统 */
    unregister_filesystem(&myfs_fs_type);

    /* 销毁inode缓存 */
    myfs_destroy_inodecache();

    /* 清理日志系统 */
    myfs_log_exit();

    printk(KERN_INFO "MyFS: Filesystem module unloaded\n");
}

/* ==================== 工具函数的临时实现 ==================== */

/* 这些函数将在其他模块中完整实现 */
unsigned long myfs_count_free_blocks(struct super_block *sb)
{
    struct myfs_sb_info *sbi = MYFS_SB(sb);
    return sbi->s_free_blocks_count;
}

unsigned long myfs_count_free_inodes(struct super_block *sb)
{
    struct myfs_sb_info *sbi = MYFS_SB(sb);
    return sbi->s_free_inodes_count;
}

int myfs_alloc_block(struct super_block *sb)
{
    /* 暂时返回固定值，完整实现在super.c中 */
    return 0;
}

void myfs_free_block(struct super_block *sb, unsigned long block)
{
    /* 暂时空实现，完整实现在super.c中 */
}

unsigned long myfs_alloc_inode(struct super_block *sb)
{
    /* 暂时返回固定值，完整实现在super.c中 */
    return 0;
}

void myfs_free_inode(struct super_block *sb, unsigned long ino)
{
    /* 暂时空实现，完整实现在super.c中 */
}

/* ==================== 模块入口 ==================== */

module_init(myfs_init);
module_exit(myfs_exit);