//
// Created by 28396 on 2025/6/29.
//

#include <iostream>
#include <string>
#include "filesystem.h"

const std::string DISK_FILE = "mydisk.img";
constexpr size_t DISK_SIZE_MB = 100;

void print_welcome_message() {
    std::cout << "========================================\n";
    std::cout << "  简易虚拟文件系统 (MySimpleFS) v1.0\n";
    std::cout << "========================================\n";
    std::cout << "支持的命令:\n";
    std::cout << "  touch <文件名>            - 创建空文件\n";
    std::cout << "  cat <文件名>              - 显示文件内容\n";
    std::cout << "  ls [目录路径]             - 列出目录内容\n";
    std::cout << "  mkdir <目录名>            - 创建目录\n";
    std::cout << "  rmdir <目录名>            - 删除目录\n";
    std::cout << "  rm <文件名>               - 删除文件\n";
    std::cout << "  echo <内容> <文件名>    - 写入文件内容\n";
    std::cout << "  edit <文件名>             - 编辑文件内容\n";
    std::cout << "  info [路径]               - 显示文件或目录信息\n";
    std::cout << "  help                      - 显示帮助信息\n";
    std::cout << "  exit                      - 退出系统\n";
    std::cout << "========================================\n";
}

int main() {
    SimpleFileSystem fs;
    bool first_run = false;

    std::cout << "正在检查虚拟磁盘文件...\n";

    // 尝试挂载现有的磁盘文件
    if (!fs.mount(DISK_FILE)) {
        std::cout << "没有找到现有的磁盘文件，正在创建新的虚拟磁盘 (" << DISK_SIZE_MB << "MB)...\n";
        if (!fs.format(DISK_FILE, DISK_SIZE_MB)) {
            std::cerr << "错误：无法创建虚拟磁盘文件！\n";
            return 1;
        }

        if (!fs.mount(DISK_FILE)) {
            std::cerr << "错误：无法挂载新创建的虚拟磁盘！\n";
            return 1;
        }

        // 创建一些基础目录
        fs.create_directory("/", "documents");
        fs.create_directory("/", "temp");
        first_run = true;
    }

    std::cout << "虚拟磁盘挂载成功！\n";
    if (first_run) {
        std::cout << "已创建基础目录结构。\n";
    }

    print_welcome_message();

    // 启动命令行接口
    fs.run_command_interface();

    // 退出前卸载文件系统
    fs.unmount();
    std::cout << "文件系统已安全卸载。再见！\n";

    return 0;
}