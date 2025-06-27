## 项目概述
这是一个基于Linux的简易操作系统驱动程序，能够将本地特殊文件挂载为虚拟文件系统。文件系统结构参考FAT32存储模式，具有以下特点：
- 内核版本大于等于5.4
- 支持二级目录结构
- 采用连续存储策略
- 使用空闲盘块表管理空间
- 采用全局置换FIFO算法进行页面置换
- 记录各种系统事件的日志功能

!!注意：项目运行在内核空间，需要在Linux内核环境中编译和运行，请注意项目代码的语法和风格遵循Linux内核规范。
## 项目文件结构
当前项目文件结构如下：
``` 
OS_homework/
├── CMakeLists.txt          // 主CMake配置文件
├── README.md               // 项目说明文档
├── develop.md              // 开发计划文档（当前文件）
├── main.c                  // 主入口程序
├── driver/                 // 驱动程序核心代码
│   ├── CMakeLists.txt      // 驱动程序构建配置
│   ├── fs.h                // 文件系统数据结构定义
│   └── fs.c                // 文件系统操作实现
├── tools/                  // 工具集
│   ├── CMakeLists.txt      // 工具构建配置 
│   └── mkfs.c              // 文件系统格式化工具
└── scripts/                // 脚本工具
    ├── mount.sh            // 挂载文件系统脚本
    ├── unmount.sh          // 卸载文件系统脚本
    └── setup.sh            // 环境设置脚本
```
计划添加的文件：
``` 
OS_homework/
├── driver/
│   ├── cache.h             // 缓存结构与FIFO算法定义
│   ├── cache.c             // 缓存和FIFO算法实现
│   ├── inode.c             // inode操作实现
│   ├── dir.c               // 目录操作实现
│   ├── file.c              // 文件操作实现
│   ├── super.c             // 超级块操作实现
│   └── log.c               // 日志系统实现
├── tools/
│   ├── fsck.c              // 文件系统检查工具
│   └── debug.c             // 调试工具
└── tests/                  // 测试目录
    ├── CMakeLists.txt      // 测试构建配置
    ├── test_fs.c           // 文件系统测试
    ├── test_cache.c        // 缓存测试
    └── test_perf.c         // 性能测试
```
## 当前进度
1. ✅ 已建立基本项目结构
2. ✅ 已定义文件系统数据结构（超级块、inode、目录项等）在 `driver/fs.h` 中
3. ✅ 已完成格式化工具（`tools/mkfs.c`）的初步实现
    - 支持创建文件系统超级块
    - 初始化inode区
    - 初始化空闲盘块位图
    - 创建根目录结构

## 下一阶段任务
1. **驱动程序核心实现**
    - [ ] 实现文件系统挂载功能 (`driver/fs.c`）
    - [ ] 实现VFS接口函数 (`driver/fs.c`)
    - [ ] 实现超级块操作 (`driver/super.c`)
    - [ ] 实现inode操作 (`driver/inode.c`)
    - [ ] 实现文件操作 (`driver/file.c`)
    - [ ] 实现目录操作 (`driver/dir.c`)

2. **二级目录支持**
    - [ ] 完善目录结构创建 (`driver/dir.c`)
    - [ ] 实现目录层级导航 (`driver/dir.c`)
    - [ ] 限制最大目录深度为2级 (`driver/dir.c`)

3. **内存缓冲与置换策略**
    - [ ] 设计缓冲区数据结构 (`driver/cache.h`)
    - [ ] 实现缓冲区初始化与管理 (`driver/cache.c`)
    - [ ] 实现FIFO页面置换算法 (`driver/cache.c`)
    - [ ] 集成缓存到文件操作中 (`driver/file.c`)

4. **日志功能实现**
    - [ ] 设计日志记录格式和存储方式 (`driver/log.c`)
    - [ ] 实现以下事件的日志记录：
        - [ ] 内存缓冲区状态变化
        - [ ] 磁盘信息读写
        - [ ] 目录信息更新
        - [ ] 文件输入读入内存过程
        - [ ] 页面置换过程

    - [ ] 日志输出到指定文件 (`driver/log.c`)

5. **完善工具集**
    - [ ] 增强格式化工具功能 (`tools/mkfs.c`)
    - [ ] 添加文件系统检查与修复工具 (`tools/fsck.c`)
    - [ ] 开发文件系统调试工具 (`tools/debug.c`)
    - [ ] 完善脚本工具 (`scripts/*.sh`)

6. **测试与验证**
    - [ ] 创建测试框架 (`tests/CMakeLists.txt`)
    - [ ] 编写单元测试 (`tests/test_*.c`)
    - [ ] 编写集成测试
    - [ ] 编写性能测试 (`tests/test_perf.c`)

## 实现细节
### 文件系统数据结构
已完成关键数据结构定义，包括：
- 超级块（`fs_super_block`）
- inode结构（`fs_inode`）
- 目录项（`fs_dir_entry`）

这些结构体已在 `driver/fs.h` 中定义。
### 缓存结构设计
将在 `driver/cache.h` 中定义以下结构：
``` c
struct fs_cache_entry {
    unsigned int block_no;         // 缓存的块号
    char *data;                    // 缓存的数据
    int dirty;                     // 脏标记
    struct list_head list;         // 双向链表节点
};

struct fs_cache {
    struct fs_cache_entry entries[MYFS_FIFO_CACHE_SIZE]; // 缓存项数组
    struct list_head fifo_list;                         // FIFO队列
    int used_entries;                                   // 已使用的缓存项数量
    spinlock_t lock;                                    // 自旋锁
};
```
### 连续存储策略
- 文件数据块以连续方式分配
- inode中记录起始块号和块数量
- 在 `driver/file.c` 中实现分配算法，寻找足够大的连续空闲块

### 空闲盘块管理
- 使用位图标记块的使用状态
- 位图区位置已在超级块中定义
- 在 `driver/super.c` 中实现空闲块的分配和释放

### FIFO置换算法
- 在 `driver/cache.c` 中实现固定大小的缓冲区队列（`MYFS_FIFO_CACHE_SIZE`）
- 按照先进先出的原则进行页面置换
- 在缓冲区满时，替换最早进入缓冲区的页面

### 日志系统
- 在 `driver/log.c` 中实现日志记录功能
- 日志内容应包括时间戳和操作类型
- 针对不同事件类型记录特定信息
- 日志文件应定期清理或循环使用

## 模块间依赖关系
``` 
main.c --> driver/fs.c
       --> tools/mkfs.c

driver/fs.c --> driver/super.c
            --> driver/inode.c
            --> driver/dir.c
            --> driver/file.c
            --> driver/cache.c
            --> driver/log.c

driver/file.c --> driver/cache.c
              --> driver/log.c

driver/dir.c --> driver/inode.c
             --> driver/log.c

driver/cache.c --> driver/log.c

tools/fsck.c --> driver/fs.h
tools/debug.c --> driver/fs.h
```
## 编译与构建系统
- 主 引用子目录的 `CMakeLists.txt``CMakeLists.txt`
- 分别为驱动、工具和测试生成不同的目标
- 优化编译选项，确保代码质量和性能

## 代码规范
- 遵循Linux内核代码风格
- 添加详细注释说明函数用途和实现细节
- 模块化设计，保持代码清晰可维护
- 每个源文件开头添加版权和作者信息

## 测试计划
1. 单元测试 (`tests/test_fs.c`, `tests/test_cache.c`)
    - 各个核心功能单独测试
    - 边界条件和异常情况测试

2. 集成测试
    - 文件系统挂载与卸载
    - 文件和目录操作完整流程
    - 大量数据写入与读取测试

3. 性能测试 (`tests/test_perf.c`)
    - 测量文件操作的响应时间
    - 评估FIFO置换算法的效率
    - 分析不同大小文件的存储效率

## 项目优化方向
- 提高大文件读写效率
- 优化缓存命中率
- 减少文件碎片
- 改进日志记录的性能影响
- 增加目录深度限制的可配置性

## 参考资料
- Linux VFS接口文档
- FAT32文件系统规范
- Linux内核模块编程指南
- 页面置换算法相关文献

## 时间安排
1. 核心驱动程序实现：2周
2. 缓存与FIFO算法实现：1周
3. 日志系统实现：1周
4. 工具完善与测试：1周
5. 优化与文档：1周

## 风险评估
- 连续存储可能导致严重的外部碎片问题
- FIFO算法可能不是最优的缓存替换策略
- 日志记录可能影响性能
- 二级目录深度限制可能不够灵活

后续将根据项目进展和测试结果定期更新此开发计划。