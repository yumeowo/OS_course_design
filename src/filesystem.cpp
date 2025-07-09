//
// Created by 28396 on 2025/6/29.
//

#include "filesystem.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>

// 构造函数
SimpleFileSystem::SimpleFileSystem() : mounted_(false), current_path_("/") {
    // 初始化成员变量
}

// 析构函数
SimpleFileSystem::~SimpleFileSystem() {
    if (mounted_) {
        unmount();
    }
}

// 格式化虚拟磁盘
bool SimpleFileSystem::format(const std::string& disk_file, const size_t size_mb) {
    if (mounted_) {
        return false; // 已挂载不能格式化
    }

    // 创建虚拟磁盘
    disk_ = std::make_unique<VirtualDisk>();
    if (!disk_->create(disk_file, size_mb)) {
        disk_.reset();
        return false;
    }

    std::cout << "格式化完成：" << disk_file << " (" << size_mb << "MB)" << std::endl;

    return true;
}

// 挂载文件系统
bool SimpleFileSystem::mount(const std::string& disk_file) {
    if (mounted_) {
        return false; // 已挂载
    }

    // 打开虚拟磁盘
    disk_ = std::make_unique<VirtualDisk>();
    if (!disk_->open(disk_file)) {
        disk_.reset();
        return false;
    }

    // 初始化位图
    bitmap_ = std::make_unique<FreeBitmap>();

    bitmap_->initialize();

    // 初始化位图
    bitmap_ = std::make_unique<FreeBitmap>();
    if (!bitmap_->load(disk_.get())) {
        disk_.reset();
        bitmap_.reset();
        return false;
    }

    // 创建缓存管理器
    cache_ = std::make_unique<CacheManager>(disk_.get());


    // 创建inode管理器
    inode_manager_ = std::make_unique<INodeManager>(disk_.get(), bitmap_.get());
    if (!inode_manager_->initialize()) {
        disk_.reset();
        bitmap_.reset();
        cache_.reset();
        inode_manager_.reset();
        return false;
    }

    // 创建根目录
    if (!inode_manager_->create_root_directory()) {
        disk_.reset();
        bitmap_.reset();
        cache_.reset();
        inode_manager_.reset();
        return false;
    }

    // 保存磁盘文件名并标记为已挂载
    disk_file_ = disk_file;
    mounted_ = true;
    std::cout << "文件系统已挂载：" << disk_file << std::endl;

    return true;
}

// 卸载文件系统
void SimpleFileSystem::unmount() {
    if (!mounted_) {
        return;
    }

    // 确保所有缓存数据写回磁盘
    if (cache_) {
        cache_->flush_all();
    }

    // 保存位图
    if (bitmap_) {
        bitmap_->save(disk_.get());
    }

    // 清理各组件
    inode_manager_.reset();
    cache_.reset();
    bitmap_.reset();
    disk_.reset();

    // 清空打开文件记录
    open_files_.clear();

    // 标记为未挂载
    mounted_ = false;
    std::cout << "文件系统已卸载" << std::endl;
}

// cd命令实现
bool SimpleFileSystem::change_directory(const std::string& path) {
    if (!mounted_) return false;

    const std::string target_path = normalize_path(path);
    const FileInfo info = get_file_info(target_path);

    if (info.inode_id == 0 || !info.is_directory) {
        return false;
    }

    current_path_ = target_path;
    return true;
}

// 创建文件
int SimpleFileSystem::create_file(const std::string& normalized, const std::string& content) {
    if (!mounted_) {
        return -1;
    }

    if (!is_valid_filename(normalized.substr(normalized.find_last_of('/') + 1))) {
        return -2; // 无效文件名
    }

    if (is_file_protected(normalized)) {
        return -3; // 文件被占用
    }

    // 创建文件
    if (!inode_manager_->create_file(normalized, content)) {
        return -4; // 创建失败
    }

    return 0; // 成功
}

// 删除文件
int SimpleFileSystem::delete_file(const std::string& normalized) {
    if (!mounted_) {
        return -1;
    }

    if (is_file_protected(normalized)) {
        return -2; // 文件被占用
    }

    // 获取文件信息确认是文件不是目录
    const FileInfo info = inode_manager_->get_file_info(normalized);
    if (info.inode_id == 0 || info.is_directory) {
        return -3; // 文件不存在或是目录
    }

    // 删除文件
    if (!inode_manager_->delete_file(normalized)) {
        return -4; // 删除失败
    }

    return 0; // 成功
}

// 读取文件内容
int SimpleFileSystem::read_file(const std::string& path, std::string& content) {
    if (!mounted_) {
        return -1;
    }

    const std::string normalized_path = normalize_path(path);

    // 读取文件不需要互斥锁，但需要增加引用计数
    open_file(normalized_path);

    // 读取文件内容
    const bool success = inode_manager_->read_file(normalized_path, content);

    // 减少引用计数
    close_file(normalized_path);

    return success ? 0 : -2;
}

// 写入文件内容
int SimpleFileSystem::write_file(const std::string& path, const std::string& content) {
    if (!mounted_) {
        return -1;
    }

    const std::string normalized_path = normalize_path(path);
    if (is_file_protected(normalized_path)) {
        return -2; // 文件被占用
    }

    // 检查文件是否存在
    const FileInfo info = inode_manager_->get_file_info(normalized_path);
    if (info.inode_id == 0) {
        // 文件不存在，创建新文件
        return create_file(normalized_path, content);
    }

    // 写入文件内容
    if (!inode_manager_->write_file(normalized_path, content)) {
        return -3; // 写入失败
    }

    return 0; // 成功
}

// 创建目录
int SimpleFileSystem::create_directory(const std::string& parent_path, const std::string& name) const
{
    if (!mounted_) {
        return -1;
    }

    if (!is_valid_filename(name)) {
        return -2; // 无效目录名
    }

    // 创建目录
    if (!inode_manager_->create_directory(parent_path, name)) {
        return -3; // 创建失败
    }

    return 0; // 成功
}

// 删除目录
int SimpleFileSystem::delete_directory(const std::string& normalized) {
    if (!mounted_) {
        return -1;
    }

    // 获取目录信息确认是目录不是文件
    const FileInfo info = inode_manager_->get_file_info(normalized);
    if (info.inode_id == 0 || !info.is_directory) {
        return -2; // 目录不存在或是文件
    }

    // 检查是否有打开的文件在此目录下
    for (const auto& [file_path, count] : open_files_) {
        if (file_path.find(normalized) == 0) {
            return -3; // 目录下有文件被占用
        }
    }

    // 删除目录
    if (!inode_manager_->delete_directory(normalized)) {
        return -4; // 删除失败
    }

    return 0; // 成功
}

// 列出目录内容
std::vector<FileInfo> SimpleFileSystem::list_directory(const std::string& path)
{
    if (!mounted_) {
        return {};
    }

    const std::string normalized_path = normalize_path(path);
    return inode_manager_->list_directory(normalized_path);
}

// 获取文件信息
FileInfo SimpleFileSystem::get_file_info(const std::string& path)
{
    if (!mounted_) {
        return {};
    }

    const std::string normalized_path = normalize_path(path);
    return inode_manager_->get_file_info(normalized_path);
}

// 打开文件（增加引用计数）
bool SimpleFileSystem::open_file(const std::string& path) {
    if (!mounted_) {
        return false;
    }

    const std::string normalized_path = normalize_path(path);

    // 检查文件是否存在
    const FileInfo info = inode_manager_->get_file_info(normalized_path);
    if (info.inode_id == 0) {
        return false; // 文件不存在
    }

    // 增加引用计数
    open_files_[normalized_path]++;
    return true;
}

// 关闭文件（减少引用计数）
bool SimpleFileSystem::close_file(const std::string& path) {
    if (!mounted_) {
        return false;
    }

    const std::string normalized_path = normalize_path(path);

    // 查找文件
    const auto it = open_files_.find(normalized_path);
    if (it == open_files_.end() || it->second <= 0) {
        return false; // 文件未打开
    }

    // 减少引用计数
    it->second--;
    if (it->second == 0) {
        open_files_.erase(it);
    }

    return true;
}

// 打印磁盘使用情况
void SimpleFileSystem::print_disk_usage() const
{
    if (!mounted_) {
        std::cout << "文件系统未挂载" << std::endl;
        return;
    }

    const uint32_t total_blocks = disk_->get_total_blocks();
    const uint32_t used_blocks = bitmap_->get_used_blocks();
    const uint32_t free_blocks = total_blocks - used_blocks;

    const double total_mb = static_cast<double>(total_blocks * BLOCK_SIZE) / (1024 * 1024);
    const double used_mb = static_cast<double>(used_blocks * BLOCK_SIZE) / (1024 * 1024);
    const double free_mb = static_cast<double>(free_blocks * BLOCK_SIZE) / (1024 * 1024);

    const double usage_percent = static_cast<double>(used_blocks) / total_blocks * 100.0;

    std::cout << "磁盘使用情况：" << std::endl;
    std::cout << "总容量: " << std::fixed << std::setprecision(2) << total_mb << " MB ("
              << total_blocks << " 块)" << std::endl;
    std::cout << "已使用: " << std::fixed << std::setprecision(2) << used_mb << " MB ("
              << used_blocks << " 块, " << usage_percent << "%)" << std::endl;
    std::cout << "空闲: " << std::fixed << std::setprecision(2) << free_mb << " MB ("
              << free_blocks << " 块, " << (100.0 - usage_percent) << "%)" << std::endl;

    const uint32_t total_inodes = inode_manager_->get_total_inodes();
    std::cout << "已使用 INode 数量: " << total_inodes << std::endl;
}

// 打印缓存状态
void SimpleFileSystem::print_cache_status() const
{
    if (!mounted_) {
        std::cout << "文件系统未挂载" << std::endl;
        return;
    }

    cache_->print_status();
}

// 规范化路径（处理"."和".."）
std::string SimpleFileSystem::normalize_path(const std::string& path) {
    std::string full_path;

    if (path.empty()) {
        // If path is empty, consider it relative to current_path_
        // However, commands should generally provide '.', current_path_, or a specific path.
        // For robustness, let's treat empty as current_path_ itself.
        full_path = current_path_;
    } else if (path[0] == '/') {
        // Absolute path
        full_path = path;
    } else {
        // Relative path
        if (current_path_ == "/") {
            full_path = "/" + path;
        } else {
            full_path = current_path_ + "/" + path;
        }
    }

    std::vector<std::string> components;
    std::stringstream ss(full_path);
    std::string item;

    // Split path by '/'
    // The first component from stringstream will be empty if full_path starts with '/', skip it.
    if (!full_path.empty() && full_path[0] == '/') {
        // consume the first empty part from leading '/'
        std::getline(ss, item, '/');
    }

    while (std::getline(ss, item, '/')) {
        if (item.empty() || item == ".") {
            // Skip empty parts (e.g., from '//') or current directory '.'
            continue;
        }
        if (item == "..") {
            // Go up one level
            if (!components.empty()) {
                components.pop_back();
            }
            // If components is empty, ".." at root level is still root.
        } else {
            components.push_back(item);
        }
    }

    // Reconstruct the path
    if (components.empty()) {
        return "/"; // Root directory
    }

    std::string result = "";
    for (const auto& comp : components) {
        result += "/" + comp;
    }

    return result;
}


// 验证文件名有效性
bool SimpleFileSystem::is_valid_filename(const std::string& name) {
    if (name.empty() || name.length() > 63) {
        return false;
    }

    // 检查非法字符
    for (const char c : name) {
        if (c == '/' || c == '\0' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|') {
            return false;
        }
    }

    return true;
}

// 检查文件是否被保护（已打开）
bool SimpleFileSystem::is_file_protected(const std::string& path) {
    const auto it = open_files_.find(path);
    return it != open_files_.end() && it->second > 0;
}

// 命令行接口
void SimpleFileSystem::run_command_interface() {
    if (!mounted_) {
        std::cout << "错误：文件系统未挂载，请先挂载文件系统" << std::endl;
        return;
    }

    std::string command;
    std::cout << this->current_path_ << " > ";

    while (std::getline(std::cin, command)) {
        if (command == "exit" || command == "quit") {
            break;
        }

        handle_command(command);
        std::cout << this->current_path_ << " > ";
    }
}

// 处理命令
void SimpleFileSystem::handle_command(const std::string& command) {
    const auto args = split_command(command);
    if (args.empty()) {
        return;
    }

    const std::string& cmd = args[0];


    if (cmd == "cd") {
        cmd_cd(args);
    } else if (cmd == "ls") {
        cmd_ls(args);
    } else if (cmd == "pwd") {
        cmd_pwd();
    } else if (cmd == "df") {
        cmd_disk_info();
    } else if (cmd == "cache") {
        cmd_cache_info();
    } else if (cmd == "stat") {
        cmd_file_info(args);
    } else if (cmd == "touch") {
        cmd_touch(args);
    } else if (cmd == "cat") {
        cmd_cat(args);
    } else if (cmd == "echo") {
        cmd_echo(args);
    } else if (cmd == "rm") {
        cmd_rm(args);
    } else if (cmd == "mkdir") {
        cmd_mkdir(args);
    } else if (cmd == "rmdir") {
        cmd_rmdir(args);
    } else if (cmd == "edit") {
        cmd_edit(args);
    } else if (cmd == "help") {
        cmd_help();
    } else {
        std::cout << "未知命令: " << cmd << std::endl;
        std::cout << "输入 'help' 获取帮助" << std::endl;
    }
}

// 分割命令为参数列表
std::vector<std::string> SimpleFileSystem::split_command(const std::string& command) {
    std::vector<std::string> args;
    std::string arg;
    bool in_quotes = false;

    for (const char c : command) {
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ' ' && !in_quotes) {
            if (!arg.empty()) {
                args.push_back(arg);
                arg.clear();
            }
        } else {
            arg += c;
        }
    }

    if (!arg.empty()) {
        args.push_back(arg);
    }

    return args;
}

// 新增的命令实现
void SimpleFileSystem::cmd_cd(const std::vector<std::string>& args) {
    if (!change_directory(args[1])) {
        std::cout << "cd: " << args[1] << ": 目录不存在" << std::endl;
    }
}

void SimpleFileSystem::cmd_pwd() const
{
    std::cout << current_path_ << std::endl;
}

void SimpleFileSystem::cmd_ls(const std::vector<std::string>& args) {
    const std::string path = args.size() > 1 ? args[1] : ".";
    const std::string full_path = normalize_path(path);

    const auto entries = list_directory(full_path);
    if (entries.empty()) {
        return; // 目录为空时直接返回
    }

    std::cout << "类型\t大小\t修改时间\t\t名称" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    for (const auto& entry : entries) {
        char time_str[32];
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S",
                     std::localtime(&entry.modify_time));

        std::cout << (entry.is_directory ? "DIR" : "FILE") << "\t"
                 << entry.size << "\t"
                 << time_str << "\t"
                 << entry.name << std::endl;
    }
}

// 拆分info命令为具体的子命令
void SimpleFileSystem::cmd_disk_info() const {
    print_disk_usage();
}

void SimpleFileSystem::cmd_cache_info() const {
    print_cache_status();
}

void SimpleFileSystem::cmd_file_info(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "用法: stat <文件路径>" << std::endl;
        return;
    }

    const FileInfo info = get_file_info(normalize_path(args[1]));
    if (info.inode_id == 0) {
        std::cout << "文件或目录不存在: " << args[1] << std::endl;
        return;
    }

    char time_str[32];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S",
                 std::localtime(&info.create_time));

    std::cout << "类型: " << (info.is_directory ? "目录" : "文件") << std::endl;
    std::cout << "大小: " << info.size << " 字节" << std::endl;
    std::cout << "创建时间: " << time_str << std::endl;
    std::cout << "INode ID: " << info.inode_id << std::endl;
}

// touch命令
void SimpleFileSystem::cmd_touch(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "用法: touch <文件路径>" << std::endl;
        return;
    }

    const int result = create_file(normalize_path(args[1]), "");

    if (result == 0) {
        std::cout << "文件创建成功: " << args[1] << std::endl;
    } else {
        std::cout << "创建文件失败，错误码: " << result << std::endl;
    }
}

// cat命令
void SimpleFileSystem::cmd_cat(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "用法: cat <文件路径>" << std::endl;
        return;
    }

    std::string content;
    const int result = read_file(args[1], content);

    if (result == 0) {
        std::cout << content << std::endl;
    } else {
        std::cout << "读取文件失败，错误码: " << result << std::endl;
    }
}

// echo命令
void SimpleFileSystem::cmd_echo(const std::vector<std::string>& args) {
    if (args.size() < 3 || args[args.size() - 2] != ">") {
        std::cout << "用法: echo <内容> > <文件路径>" << std::endl;
        return;
    }

    std::string content;
    if (args.size() > 3) {
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == ">") {
                break; // 停止在重定向符号
            }

            if (i > 1) content += " ";
            content += args[i];
        }
    }

    const int result = write_file(args.back(), content);

    if (result == 0) {
        std::cout << "写入文件成功: " << args[2] << std::endl;
    } else {
        std::cout << "写入文件失败，错误码: " << result << std::endl;
    }
}

// rm命令
void SimpleFileSystem::cmd_rm(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "用法: rm <文件路径>" << std::endl;
        return;
    }

    const int result = delete_file(normalize_path(args[1]));

    if (result == 0) {
        std::cout << "文件删除成功: " << args[1] << std::endl;
    } else {
        std::cout << "删除文件失败，错误码: " << result << std::endl;
    }
}

// mkdir命令
void SimpleFileSystem::cmd_mkdir(const std::vector<std::string>& args)
{
    if (args.size() < 2) {
        std::cout << "用法: mkdir <目录路径>" << std::endl;
        return;
    }

    const std::string normalized = normalize_path(args[1]);
    const size_t last_slash = normalized.find_last_of('/');

    const std::string parent_path = (last_slash == 0) ? "/" : normalized.substr(0, last_slash);;
    const std::string dir_name = normalized.substr(last_slash + 1);;

    const int result = create_directory(parent_path, dir_name);

    if (result == 0) {
        std::cout << "目录创建成功: " << normalized << std::endl;
    } else {
        std::cout << "创建目录失败，错误码: " << result << std::endl;
    }
}

// rmdir命令
void SimpleFileSystem::cmd_rmdir(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "用法: rmdir <目录路径>" << std::endl;
        return;
    }

    const std::string normalized = normalize_path(args[1]);
    const int result = delete_directory(normalized);

    if (result == 0) {
        std::cout << "目录删除成功: " << args[1] << std::endl;
    } else {
        std::cout << "删除目录失败，错误码: " << result << std::endl;
    }
}

// edit命令
void SimpleFileSystem::cmd_edit(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "用法: edit <文件路径>" << std::endl;
        return;
    }

    // 读取原文件内容
    std::string content;
    read_file(args[1], content);

    std::cout << "编辑模式，输入内容 (输入 '.exit' 单独一行结束编辑):" << std::endl;
    if (!content.empty()) {
        std::cout << content << std::endl;
    }

    std::string new_content;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == ".exit") {
            break;
        }

        if (!new_content.empty()) {
            new_content += "\n";
        }
        new_content += line;
    }

    const int result = write_file(args[1], new_content);

    if (result == 0) {
        std::cout << "文件保存成功: " << args[1] << std::endl;
    } else {
        std::cout << "保存文件失败，错误码: " << result << std::endl;
    }
}


// help命令
void SimpleFileSystem::cmd_help() {
    std::cout << "可用命令:" << std::endl;
    std::cout << "  cd <目录>              - 切换当前目录" << std::endl;
    std::cout << "  pwd                   - 显示当前目录" << std::endl;
    std::cout << "  df                    - 显示磁盘使用情况" << std::endl;
    std::cout << "  cache                 - 显示缓存状态" << std::endl;
    std::cout << "  stat <文件>            - 显示文件或目录信息" << std::endl;
    std::cout << "  ls [目录]              - 列出目录内容" << std::endl;
    std::cout << "  touch <文件>           - 创建空文件" << std::endl;
    std::cout << "  cat <文件>             - 显示文件内容" << std::endl;
    std::cout << "  echo <内容> > <文件>    - 写入内容到文件" << std::endl;
    std::cout << "  rm <文件>              - 删除文件" << std::endl;
    std::cout << "  mkdir <目录>           - 创建目录" << std::endl;
    std::cout << "  rmdir <目录>           - 删除目录" << std::endl;
    std::cout << "  edit <文件>            - 编辑文件内容" << std::endl;
    std::cout << "  help                  - 显示帮助信息" << std::endl;
    std::cout << "  exit                  - 退出" << std::endl;
}