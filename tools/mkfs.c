/*
 * MyFS - 文件系统格式化工具
 * 创建并初始化MyFS文件系统
 *
 * 作者: MySimpleFS项目组
 * 版本: 1.0
 * 创建时间: 2025-06-28
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../driver/myfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <getopt.h>

/* 默认配置参数 */
#define DEFAULT_BLOCK_COUNT     1024        /* 默认块数 */
#define DEFAULT_INODE_COUNT     256         /* 默认inode数 */
#define MIN_BLOCK_COUNT         16          /* 最小块数 */
#define MAX_BLOCK_COUNT         (1UL << 20) /* 最大块数(1M块) */

/* 结构体布局计算 */
struct fs_layout {
    __u32 total_blocks;                     /* 总块数 */
    __u32 inode_count;                      /* inode数量 */
    __u32 superblock_block;                 /* 超级块位置(0) */
    __u32 bitmap_start_block;               /* 位图起始块 */
    __u32 bitmap_blocks;                    /* 位图占用块数 */
    __u32 inode_table_start_block;          /* inode表起始块 */
    __u32 inode_table_blocks;               /* inode表占用块数 */
    __u32 data_start_block;                 /* 数据区起始块 */
    __u32 data_blocks;                      /* 数据块数量 */
};

/* 全局变量 */
static int verbose = 0;                     /* 详细输出模式 */
static int force = 0;                       /* 强制格式化 */

/**
 * 显示使用帮助
 */
static void show_usage(const char *progname)
{
    printf("用法: %s [选项] <设备文件>\n", progname);
    printf("\n选项:\n");
    printf("  -b, --blocks=NUM     设置总块数 (默认: %d)\n", DEFAULT_BLOCK_COUNT);
    printf("  -i, --inodes=NUM     设置inode数量 (默认: %d)\n", DEFAULT_INODE_COUNT);
    printf("  -f, --force          强制格式化，覆盖现有数据\n");
    printf("  -v, --verbose        显示详细信息\n");
    printf("  -h, --help           显示此帮助信息\n");
    printf("\n示例:\n");
    printf("  %s /tmp/myfs.img\n", progname);
    printf("  %s -b 2048 -i 512 -v /dev/loop0\n", progname);
    printf("\n注意:\n");
    printf("  - 块数范围: %d - %lu\n", MIN_BLOCK_COUNT, MAX_BLOCK_COUNT);
    printf("  - 每个块大小: %d 字节\n", MYFS_BLOCK_SIZE);
    printf("  - 最大文件大小: %llu 字节\n", MYFS_MAX_FILE_SIZE);
}

/**
 * 计算文件系统布局
 */
static int calculate_layout(struct fs_layout *layout, __u32 blocks, __u32 inodes)
{
    if (blocks < MIN_BLOCK_COUNT || blocks > MAX_BLOCK_COUNT) {
        fprintf(stderr, "错误: 块数 %u 超出范围 [%d, %lu]\n",
                blocks, MIN_BLOCK_COUNT, MAX_BLOCK_COUNT);
        return -1;
    }

    layout->total_blocks = blocks;
    layout->inode_count = inodes;

    /* 超级块占用第0块 */
    layout->superblock_block = 0;

    /* 计算位图大小 (每个位对应一个块) */
    layout->bitmap_blocks = (blocks + (MYFS_BLOCK_SIZE * 8) - 1) / (MYFS_BLOCK_SIZE * 8);
    layout->bitmap_start_block = 1;

    /* 计算inode表大小 */
    layout->inode_table_blocks = (inodes * sizeof(struct myfs_inode) + MYFS_BLOCK_SIZE - 1) / MYFS_BLOCK_SIZE;
    layout->inode_table_start_block = layout->bitmap_start_block + layout->bitmap_blocks;

    /* 计算数据区 */
    layout->data_start_block = layout->inode_table_start_block + layout->inode_table_blocks;

    if (layout->data_start_block >= blocks) {
        fprintf(stderr, "错误: 元数据占用过多空间，无法创建数据区\n");
        fprintf(stderr, "需要至少 %u 个块用于元数据\n", layout->data_start_block);
        return -1;
    }

    layout->data_blocks = blocks - layout->data_start_block;

    if (verbose) {
        printf("文件系统布局:\n");
        printf("  总块数:         %u\n", layout->total_blocks);
        printf("  inode数量:      %u\n", layout->inode_count);
        printf("  超级块:         块 %u\n", layout->superblock_block);
        printf("  位图区:         块 %u-%u (%u 块)\n",
               layout->bitmap_start_block,
               layout->bitmap_start_block + layout->bitmap_blocks - 1,
               layout->bitmap_blocks);
        printf("  inode表:        块 %u-%u (%u 块)\n",
               layout->inode_table_start_block,
               layout->inode_table_start_block + layout->inode_table_blocks - 1,
               layout->inode_table_blocks);
        printf("  数据区:         块 %u-%u (%u 块)\n",
               layout->data_start_block,
               layout->data_start_block + layout->data_blocks - 1,
               layout->data_blocks);
        printf("  文件系统大小:   %u KB (%u MB)\n",
               blocks * MYFS_BLOCK_SIZE / 1024,
               blocks * MYFS_BLOCK_SIZE / (1024 * 1024));
    }

    return 0;
}

/**
 * 写入超级块
 */
static int write_superblock(int fd, const struct fs_layout *layout)
{
    struct myfs_super_block sb;
    time_t now = time(NULL);

    if (verbose) {
        printf("正在写入超级块...\n");
    }

    /* 清零超级块 */
    memset(&sb, 0, sizeof(sb));

    /* 设置超级块字段 */
    sb.s_magic = MYFS_MAGIC;
    sb.s_blocks_count = layout->total_blocks;
    sb.s_inodes_count = layout->inode_count;
    sb.s_free_blocks_count = layout->data_blocks - 1; /* 减去根目录占用的块 */
    sb.s_free_inodes_count = layout->inode_count - 1; /* 减去根目录inode */
    sb.s_first_data_block = layout->data_start_block;
    sb.s_block_size = MYFS_BLOCK_SIZE;
    sb.s_inode_size = sizeof(struct myfs_inode);

    /* 位图信息 */
    sb.s_bitmap_block = layout->bitmap_start_block;
    sb.s_bitmap_blocks = layout->bitmap_blocks;

    /* inode表信息 */
    sb.s_inode_table_block = layout->inode_table_start_block;
    sb.s_inode_table_blocks = layout->inode_table_blocks;

    /* 文件系统状态 */
    sb.s_state = MYFS_VALID_FS;
    sb.s_errors = 0;

    /* 时间戳 */
    sb.s_lastcheck = now;
    sb.s_checkinterval = 30 * 24 * 3600; /* 30天检查一次 */
    sb.s_creator_os = 1; /* Linux */
    sb.s_rev_level = 1;

    /* 定位到第0块并写入 */
    if (lseek(fd, 0, SEEK_SET) == -1) {
        perror("lseek");
        return -1;
    }

    if (write(fd, &sb, sizeof(sb)) != sizeof(sb)) {
        perror("写入超级块失败");
        return -1;
    }

    /* 填充剩余的块空间 */
    char zero_buf[MYFS_BLOCK_SIZE - sizeof(sb)];
    memset(zero_buf, 0, sizeof(zero_buf));
    if (write(fd, zero_buf, sizeof(zero_buf)) != sizeof(zero_buf)) {
        perror("填充超级块失败");
        return -1;
    }

    return 0;
}

/**
 * 初始化位图
 */
static int init_bitmap(int fd, const struct fs_layout *layout)
{
    if (verbose) {
        printf("正在初始化位图...\n");
    }

    /* 分配位图缓冲区 */
    size_t bitmap_size = layout->bitmap_blocks * MYFS_BLOCK_SIZE;
    unsigned char *bitmap = calloc(1, bitmap_size);
    if (!bitmap) {
        perror("分配位图内存失败");
        return -1;
    }

    /* 标记已使用的块 */
    __u32 used_blocks = layout->data_start_block + 1; /* 包括根目录块 */
    for (__u32 i = 0; i < used_blocks; i++) {
        bitmap[i / 8] |= (1 << (i % 8));
    }

    /* 定位到位图区并写入 */
    off_t bitmap_offset = layout->bitmap_start_block * MYFS_BLOCK_SIZE;
    if (lseek(fd, bitmap_offset, SEEK_SET) == -1) {
        perror("lseek");
        free(bitmap);
        return -1;
    }

    if (write(fd, bitmap, bitmap_size) != bitmap_size) {
        perror("写入位图失败");
        free(bitmap);
        return -1;
    }

    free(bitmap);
    return 0;
}

/**
 * 初始化inode表
 */
static int init_inode_table(int fd, const struct fs_layout *layout)
{
    if (verbose) {
        printf("正在初始化inode表...\n");
    }

    /* 分配inode表缓冲区 */
    size_t inode_table_size = layout->inode_table_blocks * MYFS_BLOCK_SIZE;
    struct myfs_inode *inode_table = calloc(1, inode_table_size);
    if (!inode_table) {
        perror("分配inode表内存失败");
        return -1;
    }

    /* 创建根目录inode */
    struct myfs_inode *root_inode = &inode_table[MYFS_ROOT_INO - 1];
    time_t now = time(NULL);

    root_inode->i_mode = 0755 | (1 << 14); /* S_IFDIR | 0755 */
    root_inode->i_uid = 0;
    root_inode->i_gid = 0;
    root_inode->i_links_count = 2; /* . 和 .. */
    root_inode->i_size = MYFS_BLOCK_SIZE;
    root_inode->i_blocks = 1;
    root_inode->i_flags = 0;
    root_inode->i_start_block = layout->data_start_block;
    root_inode->i_block_count = 1;
    root_inode->i_atime = now;
    root_inode->i_ctime = now;
    root_inode->i_mtime = now;
    root_inode->i_dtime = 0;
    root_inode->i_parent_ino = MYFS_ROOT_INO; /* 根目录的父目录是自己 */
    root_inode->i_dir_level = 0;

    /* 定位到inode表并写入 */
    off_t inode_table_offset = layout->inode_table_start_block * MYFS_BLOCK_SIZE;
    if (lseek(fd, inode_table_offset, SEEK_SET) == -1) {
        perror("lseek");
        free(inode_table);
        return -1;
    }

    if (write(fd, inode_table, inode_table_size) != inode_table_size) {
        perror("写入inode表失败");
        free(inode_table);
        return -1;
    }

    free(inode_table);
    return 0;
}

/**
 * 创建根目录
 */
static int create_root_directory(int fd, const struct fs_layout *layout)
{
    if (verbose) {
        printf("正在创建根目录...\n");
    }

    /* 分配根目录数据块 */
    char *dir_block = calloc(1, MYFS_BLOCK_SIZE);
    if (!dir_block) {
        perror("分配根目录内存失败");
        return -1;
    }

    /* 创建 . 和 .. 目录项 */
    struct myfs_dir_entry *entry;
    char *ptr = dir_block;

    /* . 目录项 */
    entry = (struct myfs_dir_entry *)ptr;
    entry->inode = MYFS_ROOT_INO;
    entry->name_len = 1;
    entry->file_type = MYFS_FT_DIR;
    strcpy(entry->name, ".");
    entry->rec_len = sizeof(struct myfs_dir_entry) - MYFS_MAX_NAME_LEN + 1;
    ptr += entry->rec_len;

    /* .. 目录项 */
    entry = (struct myfs_dir_entry *)ptr;
    entry->inode = MYFS_ROOT_INO;
    entry->name_len = 2;
    entry->file_type = MYFS_FT_DIR;
    strcpy(entry->name, "..");
    entry->rec_len = sizeof(struct myfs_dir_entry) - MYFS_MAX_NAME_LEN + 2;

    /* 定位到根目录数据块并写入 */
    off_t root_dir_offset = layout->data_start_block * MYFS_BLOCK_SIZE;
    if (lseek(fd, root_dir_offset, SEEK_SET) == -1) {
        perror("lseek");
        free(dir_block);
        return -1;
    }

    if (write(fd, dir_block, MYFS_BLOCK_SIZE) != MYFS_BLOCK_SIZE) {
        perror("写入根目录失败");
        free(dir_block);
        return -1;
    }

    free(dir_block);
    return 0;
}

/**
 * 检查设备是否已经包含文件系统
 */
static int check_existing_fs(int fd)
{
    struct myfs_super_block sb;

    /* 读取可能存在的超级块 */
    if (lseek(fd, 0, SEEK_SET) == -1) {
        return 0; /* 假设没有现有文件系统 */
    }

    if (read(fd, &sb, sizeof(sb)) != sizeof(sb)) {
        return 0; /* 读取失败，假设没有文件系统 */
    }

    /* 检查魔数 */
    if (sb.s_magic == MYFS_MAGIC) {
        printf("警告: 设备似乎已包含MyFS文件系统\n");
        if (!force) {
            printf("使用 -f 选项强制格式化\n");
            return -1;
        }
        printf("强制格式化模式，将覆盖现有数据\n");
    }

    return 0;
}

/**
 * 主函数
 */
int main(int argc, char *argv[])
{
    int opt;
    __u32 blocks = DEFAULT_BLOCK_COUNT;
    __u32 inodes = DEFAULT_INODE_COUNT;
    const char *device = NULL;
    struct fs_layout layout;
    int fd;
    int ret = 0;

    /* 命令行选项 */
    static struct option long_options[] = {
        {"blocks",  required_argument, 0, 'b'},
        {"inodes",  required_argument, 0, 'i'},
        {"force",   no_argument,       0, 'f'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    /* 解析命令行参数 */
    while ((opt = getopt_long(argc, argv, "b:i:fvh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'b':
            blocks = strtoul(optarg, NULL, 10);
            if (blocks == 0) {
                fprintf(stderr, "错误: 无效的块数 '%s'\n", optarg);
                return 1;
            }
            break;
        case 'i':
            inodes = strtoul(optarg, NULL, 10);
            if (inodes == 0) {
                fprintf(stderr, "错误: 无效的inode数 '%s'\n", optarg);
                return 1;
            }
            break;
        case 'f':
            force = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'h':
            show_usage(argv[0]);
            return 0;
        default:
            show_usage(argv[0]);
            return 1;
        }
    }

    /* 检查设备参数 */
    if (optind >= argc) {
        fprintf(stderr, "错误: 缺少设备文件参数\n");
        show_usage(argv[0]);
        return 1;
    }
    device = argv[optind];

    /* 计算文件系统布局 */
    if (calculate_layout(&layout, blocks, inodes) < 0) {
        return 1;
    }

    /* 打开设备文件 */
    fd = open(device, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        perror("打开设备文件失败");
        return 1;
    }

    /* 检查现有文件系统 */
    if (check_existing_fs(fd) < 0) {
        close(fd);
        return 1;
    }

    /* 扩展文件大小 */
    off_t total_size = (off_t)layout.total_blocks * MYFS_BLOCK_SIZE;
    if (ftruncate(fd, total_size) < 0) {
        perror("设置文件大小失败");
        close(fd);
        return 1;
    }

    printf("正在格式化设备 %s...\n", device);

    /* 写入超级块 */
    if (write_superblock(fd, &layout) < 0) {
        ret = 1;
        goto cleanup;
    }

    /* 初始化位图 */
    if (init_bitmap(fd, &layout) < 0) {
        ret = 1;
        goto cleanup;
    }

    /* 初始化inode表 */
    if (init_inode_table(fd, &layout) < 0) {
        ret = 1;
        goto cleanup;
    }

    /* 创建根目录 */
    if (create_root_directory(fd, &layout) < 0) {
        ret = 1;
        goto cleanup;
    }

    /* 同步到磁盘 */
    if (fsync(fd) < 0) {
        perror("同步到磁盘失败");
        ret = 1;
        goto cleanup;
    }

    printf("格式化完成!\n");
    if (verbose) {
        printf("文件系统已成功创建在 %s\n", device);
        printf("可以使用以下命令挂载:\n");
        printf("  mkdir /mnt/myfs\n");
        printf("  insmod myfs.ko\n");
        printf("  mount -t myfs %s /mnt/myfs\n", device);
    }

cleanup:
    close(fd);
    return ret;
}