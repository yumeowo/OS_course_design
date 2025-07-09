//
// Created by 28396 on 2025/6/29.
//

#include <iostream>
#include <string>
#include "filesystem.h"

const std::string DISK_FILE = "mydisk.img";
constexpr size_t DISK_SIZE_MB = 256;

void print_welcome_message() {
    std::cout << "========================================\n";
    std::cout << "  简易虚拟文件系统 (MySimpleFS) v1.0\n";
    std::cout << "========================================\n";
    SimpleFileSystem::cmd_help();
    std::cout << "========================================\n";
}

int main() {
    SimpleFileSystem fs;

    std::cout << "正在检查虚拟磁盘文件...\n";

    // 步骤 1: 尝试挂载现有磁盘
    if (fs.mount(DISK_FILE)) {
        std::cout << "已成功挂载现有虚拟磁盘！\n";
    } else {
        // 步骤 2: 挂载失败，则格式化新磁盘
        std::cout << "挂载失败或磁盘不存在。正在创建并格式化新的虚拟磁盘...\n";
        if (!fs.format(DISK_FILE, DISK_SIZE_MB)) {
            std::cerr << "错误：无法创建并格式化虚拟磁盘文件！\n";
            return 1;
        }
        std::cout << "虚拟磁盘格式化成功！\n";

        // 步骤 3: 格式化后，必须再次挂载才能使用
        if (!fs.mount(DISK_FILE)) {
            std::cerr << "错误：格式化后仍然无法挂载虚拟磁盘！\n";
            return 1;
        }
        std::cout << "新创建的虚拟磁盘挂载成功！\n";

        // （可选）在新磁盘上创建基础目录
        fs.create_directory("/", "documents");
        fs.create_directory("/", "temp");
        std::cout << "基础目录已创建：/documents 和 /temp\n";
    }

    print_welcome_message();

    // 启动命令行接口
    fs.run_command_interface();

    // 退出前卸载文件系统
    fs.unmount();
    std::cout << "文件系统已安全卸载。再见！\n";

    return 0;
}