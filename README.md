# 操作系统课程设计
## 项目开发指南——by gemini
好的，很高兴能帮助你完成这个有趣且富有挑战性的项目。创建一个基于Linux内核的简易文件系统驱动程序是一个深入理解操作系统工作原理的绝佳方式。

我将为你提供一个全面的指南，涵盖项目结构、核心实现思路、关键函数的示例代码，以及如何将准备磁盘的步骤封装成可分发的脚本。我们将这个文件系统命名为 `MySimpleFS`。

-----

### 一、 项目结构建议

一个良好组织的项目结构能够让开发和维护工作事半功倍。建议如下：

```
mysimplefs/
├── fs_driver/                  # 内核驱动程序代码
│   ├── mysimplefs.c            # 文件系统驱动核心逻辑
│   ├── mysimplefs.h            # 头文件，定义磁盘结构、常量等
│   └── Makefile                # 用于编译内核模块的Makefile
│
├── mkfs_tool/                  # 用户态的格式化工具
│   ├── mkfs.mysimplefs.c       # 格式化工具源码
│   └── Makefile                # 用于编译工具的Makefile
│
├── scripts/                    # 便捷操作脚本
│   ├── setup.sh                # 一键准备磁盘镜像文件
│   ├── mount.sh                # 挂载脚本
│   └── unmount.sh              # 卸载脚本
│
└── README.md                   # 项目说明文档
```

**说明:**

* **`fs_driver/`**: 这是项目的核心，存放内核模块代码。
* **`mkfs_tool/`**: 存放一个C语言编写的用户态程序，用于将一个普通文件格式化为我们自定义的 `MySimpleFS` 格式。
* **`scripts/`**: 存放一系列bash脚本，自动化地完成磁盘镜像创建、格式化、挂载和卸载等操作，实现你“完全封装成脚本”的要求。
* **`README.md`**: 详细说明如何编译、安装、使用和卸载你的文件系统。

-----

### 二、 实现思路

我们将整个实现过程分解为几个关键步骤。

#### 1\. 定义磁盘（文件）布局 (FAT32-like)

我们需要在 `mysimplefs.h` 中用C结构体定义文件系统在磁盘镜像文件中的布局。这类似于FAT32的设计思想，但进行简化。

* **超级块 (Superblock)**：位于文件的最开始（第0块）。存放整个文件系统的元信息。

    * `magic_number`: 一个魔数，用于识别我们的文件系统。
    * `total_blocks`: 总块数。
    * `block_size`: 每块的大小（例如 4096 字节）。
    * `fat_start_block`: 文件分配表（FAT）的起始块号。
    * `fat_total_blocks`: FAT占用的总块数。
    * `data_start_block`: 数据区的起始块号。
    * `root_dir_inode_block`: 根目录的inode所在的块号。

* **文件分配表 (File Allocation Table - FAT)**：一个整数数组，紧跟在超级块之后。数组的索引代表一个数据块号，其值代表下一个数据块的块号。

    * `0`: 表示该数据块未使用。
    * `-1` (或一个特殊的大数): 表示这是文件的最后一个数据块。
    * `> 0`: 指向文件的下一个数据块的块号。

* **Inode区 和 数据区 (Inode & Data Blocks)**：为了简化，我们可以将Inode信息直接存储在它所代表的目录文件的数据块中，或者为Inode也分配独立的数据块。

    * **文件Inode**: 包含文件大小、创建/修改时间、以及第一个数据块的块号。
    * **目录Inode**: 它的数据块中存储的是一个目录项（dentry）列表。
    * **目录项 (Directory Entry)**: 包含文件名、文件类型（文件/目录）和对应的Inode块号。

#### 2\. 实现格式化工具 (`mkfs.mysimplefs`)

这是一个用户态程序，负责初始化磁盘镜像文件。

* **输入**: 一个本地文件路径（如 `mydisk.img`）和大小。
* **流程**:
    1.  打开并TRUNCATE文件到指定大小。
    2.  根据文件总大小和块大小，计算元信息（总块数、FAT大小等）。
    3.  在文件开头写入 **超级块**。
    4.  初始化 **FAT** 表，将所有块标记为未使用。
    5.  创建 **根目录**：
        * 分配一个块给根目录的Inode。
        * 分配一个块给根目录的数据区。
        * 更新FAT表，标记这两个块已被使用，并为根目录Inode指向其数据块。
        * 在根目录的数据块中写入 `.` (指向自己) 和 `..` (也指向自己，因为是根目录) 两个初始目录项。

#### 3\. 编写内核驱动 (`mysimplefs.c`)

这是最核心的部分，需要与Linux的虚拟文件系统（VFS）层交互。

* **模块初始化与注销**:

    * `mysimplefs_init()`: 调用 `register_filesystem()` 注册我们的文件系统。
    * `mysimplefs_exit()`: 调用 `unregister_filesystem()` 注销。

* **`struct file_system_type`**: 这是VFS识别我们文件系统的入口。

    * `.name`: "mysimplefs"
    * `.mount`: 指向我们的挂载函数 `mysimplefs_mount`。
    * `.kill_sb`: 指向卸载时调用的函数，用于释放超级块。

* **挂载 (`mysimplefs_mount`)**:

    * 当用户执行 `mount -t mysimplefs ...` 时，此函数被调用。
    * 它的核心任务是调用 `mount_bdev()`，并提供一个 `fill_super` 回调函数。

* **填充超级块 (`mysimplefs_fill_super`)**:

    * 这是挂载过程的重头戏。
    * **【日志点】**: 在此记录 "Mounting filesystem, reading superblock..."。
    * 从块设备（我们的磁盘镜像文件）的第0块读取数据，填充到内存中的 `struct super_block`。
    * 验证魔数是否正确。
    * **【日志点】**: 记录读取到的磁盘信息（总块数、块大小等）。
    * 读取根目录的Inode信息，并创建VFS的根 `inode` 和 `dentry`。

* **实现核心操作函数集**:

    * **`super_operations`**: 管理整个文件系统的inode。
        * `alloc_inode`, `destroy_inode`: 分配和释放内存中的inode对象。
        * `write_inode`: 将内存中inode的变更写回“磁盘”。
    * **`inode_operations`**: 对一个inode本身的操作。
        * `lookup`: 在目录中查找文件。**【日志点】**：记录 "Lookup file 'X' in directory 'Y'"。
        * `create`: 创建新文件。
        * `mkdir`: 创建新目录。
    * **`file_operations`**: 对一个打开的文件的操作。
        * `read_iter`: 读取文件内容。
        * `write_iter`: 写入文件内容。
        * `iterate_shared`: 读取目录内容（用于`ls`命令）。**【日志点】**：记录 "Reading directory 'X'"。
    * **`address_space_operations`**: VFS与页缓存（Page Cache）和后备存储（我们的磁盘镜像）之间的桥梁。这是实现日志记录的关键。
        * `readpage`: 当VFS需要的数据不在页缓存中时，会调用此函数。**【日志点】**: 在这里记录 "Page cache miss, reading block Z for file X into memory"。这就是你想要的“读入内存过程”和“换页过程”的日志点。
        * `writepage`: 当页缓存中的“脏页”需要被写回磁盘时调用。

#### 4\. 实现日志记录

最简单直接的方式是在内核中使用 `printk()` 函数。`printk` 的输出会进入内核日志缓冲区，可以通过 `dmesg` 命令查看。

为了让日志更清晰，我们可以定义一个宏：

```c
// In mysimplefs.h
#define MSFS_LOG(level, fmt, ...) \
    printk(level "[MySimpleFS] " fmt "\n", ##__VA_ARGS__)
```

然后在关键位置调用它：

```c
// In mysimplefs_fill_super()
MSFS_LOG(KERN_INFO, "Disk Info: Total Blocks=%llu, Block Size=%u", sb_info->total_blocks, sb_info->block_size);

// In mysimplefs_readpage()
MSFS_LOG(KERN_DEBUG, "readpage: Reading block %lu for inode %lu", block_to_read, page->mapping->host->i_ino);
```

**如何保存到特定文件？**

从内核空间直接写用户空间的文件是复杂且不推荐的。最佳实践是：

1.  内核驱动通过 `printk` 输出日志。
2.  在用户空间运行一个脚本或守护进程，持续读取内核日志并写入指定文件。
    `dmesg -w | grep "\[MySimpleFS\]" > /var/log/mysimplefs.log &`

-----

### 三、 示例代码片段

这里提供一些关键部分的伪代码和真实代码片段，以阐明思路。

#### 1\. `setup.sh` 脚本

```bash
#!/bin/bash

# 默认磁盘镜像大小为 32MB
DISK_SIZE_MB=32
IMAGE_FILE="mysimplefs.img"
MKFS_TOOL="./mkfs_tool/mkfs.mysimplefs"

echo "--- Creating disk image file ($IMAGE_FILE) ---"
# 使用 dd 创建一个用0填充的空文件
dd if=/dev/zero of=$IMAGE_FILE bs=1M count=$DISK_SIZE_MB

echo "--- Compiling mkfs tool ---"
make -C ./mkfs_tool/

echo "--- Formatting disk image ---"
$MKFS_TOOL $IMAGE_FILE

echo "--- Setup complete! ---"
echo "Disk image '$IMAGE_FILE' is ready."
```

#### 2\. `mkfs.mysimplefs.c` (核心逻辑)

```c
// ... (包含头文件, 定义结构体)

int main(int argc, char *argv[]) {
    // ... (参数检查)
    int fd = open(argv[1], O_RDWR);
    // ... (获取文件大小)

    // 1. 准备超级块
    struct mysimplefs_super_block sb = {
        .magic_number = MYFS_MAGIC_NUMBER,
        .block_size = 4096,
        // ... (计算其他字段)
    };

    // 2. 写入超级块
    write(fd, &sb, sizeof(sb));

    // 3. 准备并写入FAT
    uint32_t *fat = calloc(sb.fat_total_blocks, sb.block_size);
    // ... 初始化FAT，例如为根目录分配块
    fat[sb.root_dir_inode_block] = -1; // 假设inode只占一块
    lseek(fd, sb.fat_start_block * sb.block_size, SEEK_SET);
    write(fd, fat, sb.fat_total_blocks * sb.block_size);

    // 4. 准备并写入根目录 inode 和数据
    // ...

    close(fd);
    return 0;
}
```

#### 3\. `mysimplefs.c` (内核驱动)

**文件系统类型定义 & 初始化**

```c
static struct file_system_type mysimplefs_type = {
    .owner      = THIS_MODULE,
    .name       = "mysimplefs",
    .mount      = mysimplefs_mount,
    .kill_sb    = kill_block_super,
    .fs_flags   = FS_REQUIRES_DEV,
};

static int __init mysimplefs_init(void) {
    int err = register_filesystem(&mysimplefs_type);
    if (err) {
        printk(KERN_ERR "Failed to register mysimplefs\n");
        return err;
    }
    MSFS_LOG(KERN_INFO, "MySimpleFS module loaded.");
    return 0;
}

module_init(mysimplefs_init);
```

**`fill_super` 函数**

```c
int mysimplefs_fill_super(struct super_block *sb, void *data, int silent) {
    struct buffer_head *bh;
    struct mysimplefs_super_block *msfs_sb_disk;
    
    MSFS_LOG(KERN_INFO, "fill_super: Starting to mount.");

    // 读取物理设备上的超级块（第0块）
    bh = sb_bread(sb, 0);
    if (!bh) return -EIO;

    msfs_sb_disk = (struct mysimplefs_super_block *)bh->b_data;

    // 验证魔数
    if (msfs_sb_disk->magic_number != MYFS_MAGIC_NUMBER) {
        // ... 错误处理
        return -EINVAL;
    }
    
    MSFS_LOG(KERN_INFO, "Disk Info: total_blocks=%u, block_size=%u", 
             msfs_sb_disk->total_blocks, msfs_sb_disk->block_size);

    // ... 将磁盘上的超级块信息填充到 VFS 的 super_block 中
    // sb->s_magic = msfs_sb_disk->magic_number;
    // sb->s_op = &mysimplefs_sops; // 挂上 super_operations
    
    // 创建根 inode
    // root_inode = iget_locked(sb, root_inode_number);
    // ... 读取根inode信息并填充
    // sb->s_root = d_make_root(root_inode);

    brelse(bh);
    return 0;
}
```

**`address_space_operations` 的 `readpage`**

```c
static int mysimplefs_readpage(struct file *file, struct page *page) {
    struct inode *inode = page->mapping->host;
    uint64_t block_in_file = page->index; // 第几个4K页
    
    // 1. 从 inode 和 block_in_file 找到对应的物理块号
    uint32_t phys_block = mysimplefs_get_block(inode, block_in_file);

    MSFS_LOG(KERN_DEBUG, "readpage: inode=%lu, page_index=%lu, physical_block=%u", 
             inode->i_ino, page->index, phys_block);

    if (phys_block > 0) {
        struct buffer_head *bh = sb_bread(inode->i_sb, phys_block);
        if (bh) {
            // 将从磁盘读取的数据拷贝到页缓存对应的 page
            memcpy(page_address(page), bh->b_data, inode->i_sb->s_blocksize);
            brelse(bh);
        }
    }

    // 标记页面为最新，并解锁
    SetPageUptodate(page);
    unlock_page(page);
    return 0;
}

static const struct address_space_operations mysimplefs_aops = {
    .readpage = mysimplefs_readpage,
    // .writepage = mysimplefs_writepage, // 写入操作
};
```

-----

### 四、 编译和运行步骤

1.  **准备环境**: 确保已安装 `linux-headers`，版本与你的内核匹配。
2.  **创建磁盘**: `cd scripts/ && ./setup.sh`
3.  **编译驱动**: `cd fs_driver/ && make`，会生成 `mysimplefs.ko`。
4.  **加载驱动**: `sudo insmod fs_driver/mysimplefs.ko`。
5.  **准备挂载点**: `mkdir /mnt/myfs`。
6.  **挂载文件系统**:
    * 将镜像文件关联到回环设备: `sudo losetup /dev/loop20 mysimplefs.img`
    * 执行挂载: `sudo mount -t mysimplefs /dev/loop20 /mnt/myfs`
    * （或者将这两步写入 `mount.sh` 脚本）
7.  **查看日志**: `dmesg -wH`
8.  **测试**: `ls -la /mnt/myfs`, `echo "hello" > /mnt/myfs/test.txt`, `cat /mnt/myfs/test.txt`。每次操作后观察`dmesg`的输出。
9.  **卸载**: `sudo umount /mnt/myfs`, `sudo rmmod mysimplefs`, `sudo losetup -d /dev/loop20`。

这个项目挑战很大，但循序渐进，从定义磁盘结构开始，到编写格式化工具，最后再到实现内核驱动的核心回调函数，是完全可以实现的。祝你编码顺利！