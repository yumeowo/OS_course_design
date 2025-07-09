
//
// Created by 28396 on 2025/6/29.
//

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <ctime>

#include "core/disk.h"
#include "core/bitmap.h"
#include "core/inode.h"
#include "core/directory.h"
#include "core/cache.h"

class SimpleFileSystem {
private:
    std::unique_ptr<VirtualDisk> disk_;
    std::unique_ptr<FreeBitmap> bitmap_;
    std::unique_ptr<CacheManager> cache_;
    std::unique_ptr<INodeManager> inode_manager_;

    bool mounted_;
    std::string disk_file_;

    // 文件保护机制 - 跟踪打开的文件
    std::unordered_map<std::string, int> open_files_; // 文件路径 -> 打开次数

    // 内部辅助函数
    static std::string normalize_path(const std::string& path);
    static bool is_valid_filename(const std::string& name);
    bool is_file_protected(const std::string& path);

public:
    SimpleFileSystem();
    ~SimpleFileSystem();

    // 初始化和销毁
    bool format(const std::string& disk_file, size_t size_mb);
    bool mount(const std::string& disk_file);
    void unmount();

    // 文件操作
    int create_file(const std::string& path, const std::string& content = "");
    int delete_file(const std::string& path);
    int read_file(const std::string& path, std::string& content);
    int read_file_block(const std::string& path, uint32_t block_index, std::string& content);
    int write_file(const std::string& path, const std::string& content);
    int write_file_block(const std::string& path, uint32_t block_index, const std::string& content);
    int modify_file_content(const std::string& path, const std::string& new_content);

    // 目录操作
    int create_directory(const std::string& parent_path, const std::string& name) const;
    int delete_directory(const std::string& path);
    std::vector<FileInfo> list_directory(const std::string& path) const;

    // 查询功能
    std::vector<FileInfo> query_directory_info(const std::string& path = "/") const;
    FileInfo get_file_info(const std::string& path) const;

    // 文件保护
    bool open_file(const std::string& path);
    bool close_file(const std::string& path);

    // 系统信息
    void print_disk_usage() const;
    void print_cache_status() const;
    bool is_mounted() const { return mounted_; }

    // 命令行接口
    void run_command_interface();

private:
    // 命令处理函数
    void handle_command(const std::string& command);
    void cmd_touch(const std::vector<std::string>& args);
    void cmd_ls(const std::vector<std::string>& args) const;
    void cmd_cat(const std::vector<std::string>& args);
    void cmd_echo(const std::vector<std::string>& args);
    void cmd_rm(const std::vector<std::string>& args);
    void cmd_mkdir(const std::vector<std::string>& args) const;
    void cmd_rmdir(const std::vector<std::string>& args);
    void cmd_edit(const std::vector<std::string>& args);
    void cmd_info(const std::vector<std::string>& args) const;
    static void cmd_help();

    static std::vector<std::string> split_command(const std::string& command);
};

#endif //FILESYSTEM_H