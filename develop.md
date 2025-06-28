# MySimpleFS 用户空间文件系统项目开发指导书

## 项目概述
这是一个运行在用户空间的简易文件系统模拟器，使用一个普通文件作为虚拟磁盘，实现基本的文件系统功能。项目具有以下特点：
- 运行在用户空间，使用标准C++库
- 使用单个文件作为M×N大小的虚拟磁盘
- 支持二级目录结构
- 采用连续存储策略（节点链表方式）
- 使用空闲盘块表管理磁盘空间
- 实现M×K大小的缓冲页链表，采用全局FIFO置换策略
- 使用线程模拟多进程环境，支持简单的进程调度和同步

## 设计架构

### 核心组件
1. **虚拟磁盘管理** - 基于文件的磁盘块读写
2. **空闲盘块表** - 简单的位图管理
3. **节点链表** - 连续存储的文件数据管理
4. **二级目录** - 根目录 + 子目录的简单结构
5. **缓冲页管理** - FIFO置换的内存缓存
6. **进程模拟** - 基于线程的简单调度

### 系统参数（可配置）
```cpp
#define DISK_SIZE_MB 100        // 虚拟磁盘大小（MB）
#define BLOCK_SIZE 4096         // 磁盘块大小（字节）
#define CACHE_PAGES 16          // 缓冲页数量
#define MAX_FILES 1024          // 最大文件数
#define MAX_DIRS 64             // 最大目录数
#define MAX_PROCESSES 8         // 最大进程数
#define TIME_SLICE_MS 100       // 时间片（毫秒）
```
## 项目文件结构
``` 
MySimpleFS/
├── CMakeLists.txt              // 主构建配置
├── README.md                   // 项目说明
├── develop.md                  // 开发指导（本文件）
├── src/                        // 源代码目录
│   ├── core/                   // 核心模块
│   │   ├── disk.h             // 虚拟磁盘定义
│   │   ├── disk.cpp           // 虚拟磁盘实现
│   │   ├── bitmap.h           // 空闲盘块表定义
│   │   ├── bitmap.cpp         // 空闲盘块表实现
│   │   ├── inode.h            // 节点定义
│   │   ├── inode.cpp          // 节点实现
│   │   ├── directory.h        // 目录定义
│   │   ├── directory.cpp      // 目录实现
│   │   ├── cache.h            // 缓冲管理定义
│   │   └── cache.cpp          // 缓冲管理实现
│   ├── process/               // 进程模拟
│   │   ├── scheduler.h        // 调度器定义
│   │   ├── scheduler.cpp      // 调度器实现
│   │   ├── sync.h             // 同步原语定义
│   │   └── sync.cpp           // 同步原语实现
│   ├── filesystem.h           // 文件系统主接口
│   ├── filesystem.cpp         // 文件系统主实现
│   └── main.cpp               // 主程序入口
├── tools/                     // 工具程序
│   ├── format.cpp             // 格式化工具
│   └── monitor.cpp            // 系统监控工具
├── tests/                     // 测试程序
│   ├── test_basic.cpp         // 基础功能测试
│   ├── test_performance.cpp   // 性能测试
│   └── test_concurrent.cpp    // 并发测试
└── examples/                  // 示例程序
    ├── simple_demo.cpp        // 简单演示
    └── benchmark.cpp          // 性能基准测试
```
## 核心数据结构
### 虚拟磁盘
``` cpp
class VirtualDisk {
private:
    std::string disk_file_;
    size_t disk_size_;
    size_t block_size_;
    std::fstream file_stream_;
    
public:
    bool create(const std::string& filename, size_t size_mb);
    bool read_block(uint32_t block_no, void* buffer);
    bool write_block(uint32_t block_no, const void* buffer);
    uint32_t get_total_blocks() const;
};
```
### 空闲盘块表
``` cpp
class FreeBitmap {
private:
    std::vector<uint8_t> bitmap_;
    uint32_t total_blocks_;
    std::mutex mutex_;
    
public:
    bool allocate_block(uint32_t& block_no);
    bool allocate_consecutive_blocks(uint32_t count, uint32_t& start_block);
    void free_block(uint32_t block_no);
    void free_consecutive_blocks(uint32_t start_block, uint32_t count);
};
```
### 简化的INode
``` cpp
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
```
### 缓冲页
``` cpp
struct CachePage {
    uint32_t block_no;              // 缓存的块号
    bool dirty;                     // 脏标记
    time_t access_time;             // 访问时间（用于FIFO）
    std::vector<uint8_t> data;      // 缓存数据
};

class CacheManager {
private:
    std::vector<CachePage> pages_;
    std::queue<uint32_t> fifo_queue_;
    std::unordered_map<uint32_t, uint32_t> block_to_page_;
    std::mutex mutex_;
    
public:
    bool read_block(uint32_t block_no, void* buffer);
    bool write_block(uint32_t block_no, const void* buffer);
    void flush_all();
};
```
### 进程模拟
``` cpp
struct Process {
    uint32_t pid;
    std::string name;
    std::thread thread;
    std::atomic<bool> running;
    uint32_t time_slice;
    std::function<void()> task;
};

class SimpleScheduler {
private:
    std::vector<Process> processes_;
    std::queue<uint32_t> ready_queue_;
    std::mutex scheduler_mutex_;
    
public:
    uint32_t create_process(const std::string& name, std::function<void()> task);
    void schedule();
    void terminate_process(uint32_t pid);
};
```
## 实现阶段
### 第一阶段：基础磁盘和存储管理
1. **虚拟磁盘实现** (`src/core/disk.cpp`)
   - 创建固定大小的磁盘文件
   - 实现块级别的读写操作
   - 添加基本的错误检查

2. **空闲盘块表** (`src/core/bitmap.cpp`)
   - 实现简单的位图管理
   - 支持单块和连续块的分配
   - 提供块使用统计功能

### 第二阶段：文件系统核心
1. **节点管理** (`src/core/inode.cpp`)
   - 实现INode的创建、删除、修改
   - 支持连续存储的文件数据管理
   - 实现简单的文件扩展和收缩

2. **二级目录** (`src/core/directory.cpp`)
   - 实现根目录和子目录结构
   - 限制目录深度为2级
   - 提供目录遍历和查找功能

### 第三阶段：缓冲管理
1. **FIFO缓存** (`src/core/cache.cpp`)
   - 实现固定大小的缓冲页池
   - 实现FIFO置换算法
   - 支持脏页回写机制

### 第四阶段：进程模拟
1. **简单调度器** (`src/process/scheduler.cpp`)
   - 基于时间片轮转的RR调度
   - 使用C++线程模拟进程
   - 实现基本的进程同步原语

2. **同步机制** (`src/process/sync.cpp`)
   - 使用std::mutex实现互斥
   - 使用std::condition_variable实现同步
   - 实现简单的信号量机制

### 第五阶段：集成和测试
1. **文件系统接口** (`src/filesystem.cpp`)
   - 提供统一的文件系统API
   - 集成所有核心模块
   - 实现基本的文件操作（创建、读取、写入、删除）

2. **工具和测试**
   - 格式化工具
   - 性能监控工具
   - 单元测试和集成测试

## API设计
### 主要接口
``` cpp
class SimpleFileSystem {
public:
    // 初始化和销毁
    bool format(const std::string& disk_file, size_t size_mb);
    bool mount(const std::string& disk_file);
    void unmount();
    
    // 文件操作
    int create_file(const std::string& path, const std::string& name);
    int delete_file(const std::string& path);
    int read_file(const std::string& path, void* buffer, size_t size, size_t offset);
    int write_file(const std::string& path, const void* buffer, size_t size, size_t offset);
    
    // 目录操作
    int create_directory(const std::string& path, const std::string& name);
    int delete_directory(const std::string& path);
    std::vector<std::string> list_directory(const std::string& path);
    
    // 系统信息
    void print_disk_usage();
    void print_cache_status();
    void print_process_status();
};
```
## 演示程序设计
### 主演示程序
``` cpp
int main() {
    SimpleFileSystem fs;
    
    // 1. 格式化虚拟磁盘
    fs.format("virtual_disk.img", 50);  // 50MB
    fs.mount("virtual_disk.img");
    
    // 2. 创建演示文件和目录
    fs.create_directory("/", "documents");
    fs.create_directory("/", "temp");
    
    // 3. 启动多个模拟进程进行文件操作
    std::vector<std::thread> workers;
    
    // 进程1：写入文件
    workers.emplace_back([&fs]() {
        std::string data = "Hello, SimpleFS!";
        fs.create_file("/documents", "test.txt");
        fs.write_file("/documents/test.txt", data.c_str(), data.size(), 0);
    });
    
    // 进程2：读取文件
    workers.emplace_back([&fs]() {
        char buffer[1024];
        fs.read_file("/documents/test.txt", buffer, sizeof(buffer), 0);
        std::cout << "Read: " << buffer << std::endl;
    });
    
    // 等待所有进程完成
    for (auto& worker : workers) {
        worker.join();
    }
    
    // 4. 显示系统状态
    fs.print_disk_usage();
    fs.print_cache_status();
    
    fs.unmount();
    return 0;
}
```
## 构建配置
### CMakeLists.txt
``` cmake
cmake_minimum_required(VERSION 3.10)
project(MySimpleFS)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# 源文件
file(GLOB_RECURSE SOURCES "src/*.cpp")

# 可执行文件
add_executable(simple_fs ${SOURCES})

# 链接线程库
find_package(Threads REQUIRED)
target_link_libraries(simple_fs Threads::Threads)

# 工具程序
add_executable(format_tool tools/format.cpp src/core/disk.cpp src/core/bitmap.cpp)
target_link_libraries(format_tool Threads::Threads)

# 测试程序
enable_testing()
add_subdirectory(tests)
```
## 开发计划
### 时间安排
- 第1周：虚拟磁盘和位图管理
- 第2周：节点和目录系统
- 第3周：缓冲管理和FIFO算法
- 第4周：进程模拟和调度
- 第5周：集成测试和优化

### 验收标准
1. 能够创建和管理虚拟磁盘文件
2. 支持二级目录结构的文件操作
3. 实现连续存储和空闲块管理
4. 缓冲系统能够正确执行FIFO置换
5. 多线程环境下的基本同步和调度
6. 提供完整的演示程序

## 技术特点
### 简化设计
- 去除所有内核相关的复杂性
- 使用标准C++库，避免平台兼容问题
- 专注于核心算法实现
- 最小化外部依赖

### 可扩展性
- 模块化设计，易于理解和修改
- 清晰的接口定义
- 完整的测试覆盖
- 详细的文档和注释
