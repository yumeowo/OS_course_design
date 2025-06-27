#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

// 引入我们自己的文件系统结构定义
#include "../driver/fs.h"

// 宏定义用于简化日志输出
#define LOG(fmt, ...) printf("[MKFS] " fmt "\n", ##__VA_ARGS__)

// 位图操作辅助函数
static inline void set_bit(int n, unsigned char *bitmap) {
    bitmap[n / 8] |= (1 << (n % 8));
}

int main(const int argc, char *argv[]) {
    int fd;
    struct stat st;
    ssize_t written;

    if (argc != 2) {
        fprintf(stderr, "用法: %s <设备或镜像文件>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *device_path = argv[1];

    // 1. 打开设备文件
    fd = open(device_path, O_RDWR);
    if (fd < 0) {
        perror("打开设备失败");
        return EXIT_FAILURE;
    }

    if (fstat(fd, &st) < 0) {
        perror("获取设备信息失败");
        close(fd);
        return EXIT_FAILURE;
    }

    LOG("成功打开设备: %s, 大小: %ld 字节", device_path, st.st_size);

    // 2. 计算文件系统布局
    struct fs_super_block sb = {0};
    sb.magic_number = MYFS_MAGIC_NUMBER;
    sb.block_size = MYFS_DEFAULT_BLOCK_SIZE;
    sb.total_blocks = st.st_size / sb.block_size;

    // Inode区占用总块数的10% (简单估算)
    sb.inode_total_blocks = (sb.total_blocks * 10) / 100;
    sb.inode_start_block = 1; // 超级块占用第0块
    sb.max_inodes = (sb.inode_total_blocks * sb.block_size) / sizeof(struct fs_inode);

    // 位图区紧随Inode区之后
    sb.free_block_bitmap_total_blocks = (sb.total_blocks + 8 * sb.block_size - 1) / (8 * sb.block_size);
    sb.free_block_bitmap_start_block = sb.inode_start_block + sb.inode_total_blocks;

    // 数据区紧随位图区之后
    sb.data_start_block = sb.free_block_bitmap_start_block + sb.free_block_bitmap_total_blocks;

    // 设置根目录inode编号为0
    sb.root_dir_inode = 0;
    sb.state = 0; // 初始状态为 clean

    LOG("文件系统布局计算完成:");
    LOG("  - 超级块 (Superblock)    : 块 0");
    LOG("  - Inode区                : 起始块 %u, 共 %u 块, 最多 %u 个 Inode", sb.inode_start_block, sb.inode_total_blocks, sb.max_inodes);
    LOG("  - 空闲盘块位图           : 起始块 %u, 共 %u 块", sb.free_block_bitmap_start_block, sb.free_block_bitmap_total_blocks);
    LOG("  - 数据区                 : 起始块 %u", sb.data_start_block);

    // 3. 写入超级块
    lseek(fd, 0, SEEK_SET);
    written = write(fd, &sb, sizeof(sb));
    if (written != sizeof(sb)) {
        perror("写入超级块失败");
        close(fd);
        return EXIT_FAILURE;
    }
    LOG("超级块已成功写入。");

    // 4. 初始化并写入Inode区
    struct fs_inode *inode_table = calloc(sb.inode_total_blocks, sb.block_size);
    if (!inode_table) {
        perror("为Inode区分配内存失败");
        close(fd);
        return EXIT_FAILURE;
    }

    // 初始化根目录 Inode (inode 0)
    struct fs_inode *root_inode = &inode_table[0];
    root_inode->i_mode = S_IFDIR | 0755; // 目录文件，权限rwx|r-x|r-x
    root_inode->i_n_links = 2; // '.' 和 '..'
    root_inode->i_size = sb.block_size; // 初始大小为一个块
    root_inode->i_start_block = sb.data_start_block; // 分配第一个数据块给根目录
    root_inode->i_blocks_count = 1;
    root_inode->i_ctime = root_inode->i_mtime = root_inode->i_atime = time(NULL);
    root_inode->i_parent_inode = 0; // 根目录的父目录是其自身
    root_inode->i_dir_level = 0;

    lseek(fd, sb.inode_start_block * sb.block_size, SEEK_SET);
    written = write(fd, inode_table, sb.inode_total_blocks * sb.block_size);
    if (written != sb.inode_total_blocks * sb.block_size) {
        perror("写入Inode区失败");
        free(inode_table);
        close(fd);
        return EXIT_FAILURE;
    }
    LOG("Inode区已成功初始化并写入。");
    free(inode_table);

    // 5. 初始化并写入根目录的数据块
    char *root_data_block = calloc(1, sb.block_size);
    if (!root_data_block) {
        perror("为根目录数据块分配内存失败");
        close(fd);
        return EXIT_FAILURE;
    }

    // 创建 '.' 目录项
    struct fs_dir_entry *entry = (struct fs_dir_entry *)root_data_block;
    entry->inode_num = 0;
    strcpy(entry->name, ".");
    entry->name_len = 1;
    entry->file_type = 2; // 目录
    entry->rec_len = sizeof(struct fs_dir_entry) - MYFS_FILENAME_MAX_LEN -1 + entry->name_len;

    // 创建 '..' 目录项
    struct fs_dir_entry *parent_entry = (struct fs_dir_entry *)(root_data_block + entry->rec_len);
    parent_entry->inode_num = 0;
    strcpy(parent_entry->name, "..");
    parent_entry->name_len = 2;
    parent_entry->file_type = 2; // 目录
    parent_entry->rec_len = sb.block_size - entry->rec_len; // 占据剩余空间

    lseek(fd, sb.data_start_block * sb.block_size, SEEK_SET);
    written = write(fd, root_data_block, sb.block_size);
     if (written != sb.block_size) {
        perror("写入根目录数据块失败");
        free(root_data_block);
        close(fd);
        return EXIT_FAILURE;
    }
    LOG("根目录数据块已成功创建并写入。");
    free(root_data_block);

    // 6. 初始化并写入空闲盘块位图
    unsigned char *bitmap = calloc(sb.free_block_bitmap_total_blocks, sb.block_size);
    if(!bitmap){
        perror("为位图分配内存失败");
        close(fd);
        return EXIT_FAILURE;
    }

    // 标记所有元数据块和根目录数据块为"已使用"
    for (int i = 0; i < sb.data_start_block + 1; i++) {
        set_bit(i, bitmap);
    }

    lseek(fd, sb.free_block_bitmap_start_block * sb.block_size, SEEK_SET);
    written = write(fd, bitmap, sb.free_block_bitmap_total_blocks * sb.block_size);
    if(written != sb.free_block_bitmap_total_blocks * sb.block_size){
        perror("写入位图失败");
        free(bitmap);
        close(fd);
        return EXIT_FAILURE;
    }
    LOG("空闲盘块位图已成功初始化并写入。");
    free(bitmap);

    // 7. 完成
    close(fd);
    LOG("文件系统格式化成功! 设备 %s 已就绪。", device_path);

    return EXIT_SUCCESS;
}