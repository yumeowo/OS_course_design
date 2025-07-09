#include "inode.h"
#include <cstring>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>

// 每个INode结构体大小（字节）
constexpr uint32_t INODE_SIZE = sizeof(INode);

// 每块可存储的INode数量
constexpr uint32_t INODES_PER_BLOCK = BLOCK_SIZE / INODE_SIZE;

INodeManager::INodeManager(VirtualDisk* disk, FreeBitmap* bitmap) 
    : disk_(disk), bitmap_(bitmap) {
    // 初始化inode使用标记
    inode_used_.resize(max_inodes_, false);

    // 初始化细粒度锁
    inode_locks_.resize(max_inodes_);
    for (size_t i = 0; i < max_inodes_; ++i) {
        inode_locks_[i] = std::make_unique<SpinLock>();
    }
}

INodeManager::~INodeManager() {
    // 清理目录缓存
    LockGuard<SimpleMutex> lock(cache_mutex_);
    directory_cache_.clear();
}

bool INodeManager::initialize()
{
    // ReadWriteLock::WriteGuard write_guard(inode_lock_);

    // 初始化超级块和inode表
    // 这里简化实现，实际中应该从磁盘读取或初始化
    return true;
}
bool INodeManager::create_root_directory() {

    // 确保位图已初始化
    if (!bitmap_) {
        std::cerr << "Error: 位图未初始化" << std::endl;
        return false;
    }

    // 创建根目录inode
    INode root_inode;
    root_inode.id = ROOT_INODE_ID;
    root_inode.type = FS_DIRECTORY;
    root_inode.size = 0;
    root_inode.block_count = 1;
    root_inode.parent_id = ROOT_INODE_ID;
    root_inode.create_time = time(nullptr);
    root_inode.modify_time = root_inode.create_time;
    strcpy(root_inode.name, "/");

    // 分配数据块
    if (!bitmap_->allocate_consecutive_blocks(1, root_inode.start_block)) {
        std::cerr << "Error: 无法为根目录分配数据块" << std::endl;
        return false;
    }

    // 写入inode
    if (!write_inode(ROOT_INODE_ID, &root_inode)) {
        std::cerr << "Error: 写入根目录inode失败" << std::endl;
        bitmap_->free_consecutive_blocks(root_inode.start_block, 1);
        return false;
    }

    // 标记inode为已使用
    inode_used_[ROOT_INODE_ID] = true;
    inode_count_++;

    // 创建目录对象
    auto root_dir = std::make_unique<Directory>(ROOT_INODE_ID);
    if (!root_dir) {
        std::cerr << "Error: 创建根目录对象失败" << std::endl;
        bitmap_->free_consecutive_blocks(root_inode.start_block, 1);
        return false;
    }

    // 添加 . 和 .. 目录项
    if (!root_dir->add_entry(".", ROOT_INODE_ID, FS_DIRECTORY) ||
        !root_dir->add_entry("..", ROOT_INODE_ID, FS_DIRECTORY)) {
        std::cerr << "Error: 添加根目录项失败" << std::endl;
        bitmap_->free_consecutive_blocks(root_inode.start_block, 1);
        return false;
    }

    // 保存目录内容
    if (!save_directory_content(ROOT_INODE_ID, *root_dir)) {
        std::cerr << "Error: 保存根目录内容失败" << std::endl;
        bitmap_->free_consecutive_blocks(root_inode.start_block, 1);
        delete_inode(ROOT_INODE_ID);
        return false;
    }


    // 缓存目录
    cache_directory(ROOT_INODE_ID, std::move(root_dir));

    return true;
}

int32_t INodeManager::create_inode(const uint32_t parent_id, const uint8_t type,
                                   const std::string& name, const uint32_t size) {

    if (inode_count_ >= max_inodes_) {
        return -1; // 无可用 inode
    }

    // 寻找未使用的 inode 槽位需要原子操作
    int32_t inode_id = -1;
    {
        LockGuard<SimpleMutex> alloc_guard(allocation_mutex_);
        for (uint32_t i = 1; i < max_inodes_; ++i) {
            if (!inode_used_[i]) {
                inode_id = i;
                inode_used_[i] = true;  // 立即标记为已使用
                break;
            }
        }
    }
    if (inode_id == -1) {
        std::cerr << "No free inodes available" << std::endl;
        return -1;
    }

    // 分配连续块
    uint32_t block_count = calculate_blocks_needed(size);
    uint32_t start_block = 0;

    if (type == FS_FILE) {
        if (!bitmap_->allocate_consecutive_blocks(block_count, start_block)) {
            inode_used_[inode_id] = false; // 失败时回滚
            return -2;
        }
    } else {
        if (!bitmap_->allocate_consecutive_blocks(1, start_block)) {
            inode_used_[inode_id] = false; // 失败时回滚
            return -2;
        }
        block_count = 1;
    }

    INode new_node{
        .id = static_cast<uint32_t>(inode_id),
        .type = type,
        .size = size,
        .start_block = start_block,
        .block_count = block_count,
        .parent_id = parent_id,
        .create_time = time(nullptr),
        .modify_time = time(nullptr)
    };
    strncpy(new_node.name, name.c_str(), sizeof(new_node.name) - 1);
    new_node.name[sizeof(new_node.name) - 1] = '\0';

    if (!write_inode(new_node.id, &new_node)) {
        bitmap_->free_consecutive_blocks(start_block, block_count);
        inode_used_[inode_id] = false; // 写入失败，回滚
        return -3;
    }

    // 更新父目录
    if (parent_id != inode_id) { // 不是根目录
        if (!add_directory_entry(parent_id, name, inode_id, type)) {
            delete_inode(inode_id);
            return -1;
        }
    }

    inode_used_[inode_id] = true;
    inode_count_++;
    return inode_id;
}

bool INodeManager::read_inode(const uint32_t inode_id, INode* node) const
{
    if (inode_id >= max_inodes_) return false;

    if (!inode_used_[inode_id]) return false;

    const uint32_t block_index = inode_id / INODES_PER_BLOCK + inode_table_start_;
    const uint32_t block_offset = (inode_id % INODES_PER_BLOCK) * INODE_SIZE;

    std::vector<uint8_t> block(BLOCK_SIZE);
    if (!disk_->read_block(block_index, block.data())) return false;

    memcpy(node, block.data() + block_offset, INODE_SIZE);
    return true;
}

bool INodeManager::write_inode(const uint32_t inode_id, const INode* node) const
{
    if (inode_id >= max_inodes_) return false;

    const uint32_t block_index = inode_id / INODES_PER_BLOCK + inode_table_start_;
    const uint32_t block_offset = (inode_id % INODES_PER_BLOCK) * INODE_SIZE;

    std::vector<uint8_t> block(BLOCK_SIZE);
    if (!disk_->read_block(block_index, block.data()))
    {
        return false;
    }

    memcpy(block.data() + block_offset, node, INODE_SIZE);
    return disk_->write_block(block_index, block.data());
}

bool INodeManager::delete_inode(const uint32_t inode_id) {
    if (inode_id >= MAX_FILES || !inode_used_[inode_id]) return false;

    INode node;
    if (!read_inode(inode_id, &node)) return false;

    // 从父目录移除
    if (node.parent_id != inode_id) { // 不是根目录
        remove_directory_entry(node.parent_id, node.name);
    }

    if (node.block_count > 0) {
        bitmap_->free_consecutive_blocks(node.start_block, node.block_count);
    }

    // 从缓存中移除
    remove_from_cache(inode_id);

    // node.type = 0xFF;
    // node.size = 0;
    // node.block_count = 0;
    // node.start_block = 0;
    // node.name[0] = '\0';
    //
    // if (!write_inode(inode_id, &node)) return false;

    // 原子性地更新使用状态
    {
        LockGuard<SimpleMutex> alloc_guard(allocation_mutex_);
        inode_used_[inode_id] = false;
        inode_count_--;
    }

    return true;
}

bool INodeManager::resize_inode(const uint32_t inode_id, const uint32_t new_size) const
{
    if (inode_id >= MAX_FILES) return false;

    if (!inode_used_[inode_id]) return false;

    LockGuard<SpinLock> inode_guard(*inode_locks_[inode_id]);

    INode node;
    if (!read_inode(inode_id, &node) || node.type != FS_FILE) return false;

    const uint32_t new_blocks = calculate_blocks_needed(new_size);
    const uint32_t old_blocks = node.block_count;

    if (new_blocks == old_blocks) {
        node.size = new_size;
        node.modify_time = time(nullptr);
        return write_inode(inode_id, &node);
    }

    if (new_blocks > old_blocks) {
        const uint32_t additional = new_blocks - old_blocks;
        bool contiguous = true;

        // 检查后续 block 是否空闲（确保从 node.start_block + old_blocks 开始有足够空位）
        for (uint32_t i = 0; i < additional; ++i) {
            if (bitmap_->is_block_allocated(node.start_block + old_blocks + i)) {
                contiguous = false;
                break;
            }
        }

        if (contiguous) {
            // 直接手动标记新块为已使用
            for (uint32_t i = 0; i < additional; ++i) {
                bitmap_->mark_block_used(node.start_block + old_blocks + i); // 你需要在 FreeBitmap 里实现这个函数
            }

            node.block_count = new_blocks;
            node.size = new_size;
            node.modify_time = time(nullptr);
            return write_inode(inode_id, &node);
        }
    }

    // 不连续，重新分配块
    uint32_t new_start;
    if (!bitmap_->allocate_consecutive_blocks(new_blocks, new_start)) {
        return false;
    }

    if (!disk_->copy_blocks(node.start_block, new_start, old_blocks)) {
        bitmap_->free_consecutive_blocks(new_start, new_blocks);
        return false;
    }

    bitmap_->free_consecutive_blocks(node.start_block, old_blocks);
    node.start_block = new_start;
    node.block_count = new_blocks;
    node.size = new_size;
    node.modify_time = time(nullptr);

    return write_inode(inode_id, &node);
}

uint32_t INodeManager::calculate_blocks_needed(const uint32_t size)
{
    return (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

int32_t INodeManager::find_inode(const uint32_t parent_id, const std::string& name) const
{
    const std::shared_ptr<Directory> dir = get_directory(parent_id);
    if (!dir) {
        return -1;
    }

    DirectoryEntry entry;
    if (dir->find_entry(name, entry)) {
        return static_cast<int32_t>(entry.inode_id);
    }

    return -1;
}

uint32_t INodeManager::get_total_inodes() const {
    return inode_count_;
}

int32_t INodeManager::resolve_path(const std::string& normalized) const {

    if (normalized == "/") {
        return ROOT_INODE_ID;
    }

    const std::vector<std::string> components = split_path(normalized);
    int32_t current_inode = ROOT_INODE_ID;

    for (const auto& component : components) {
        // 查找子目录
        const auto dir = get_directory(current_inode);
        if (!dir) {
            return -1;
        }

        DirectoryEntry entry;
        if (!dir->find_entry(component, entry)) {
            return -1;
        }

        current_inode = entry.inode_id;
    }

    return current_inode;
}

bool INodeManager::create_file(const std::string& normalized, const std::string& content) {

    // 提取目录和文件名
    const size_t last_slash = normalized.find_last_of('/');
    const std::string parent_path = (last_slash == 0) ? "/" : normalized.substr(0, last_slash);
    const std::string filename = normalized.substr(last_slash + 1);

    if (!is_valid_filename(filename)) {
        return false;
    }

    // 解析父目录
    const int32_t parent_inode = resolve_path(parent_path);
    if (parent_inode == -1) {
        return false;
    }

    // 检查文件是否已存在
    if (find_inode(parent_inode, filename) != -1) {
        return false;
    }

    // 创建文件inode
    const size_t file_size = content.size() == 0 ? 1 : content.size();
    const int32_t file_inode = create_inode(parent_inode, FS_FILE, filename, file_size);
    if (file_inode == -1) {
        return false;
    }

    // 写入文件内容
    if (!content.empty()) {
        return write_file_data(file_inode, content);
    }

    return true;
}

bool INodeManager::create_directory(const std::string& parent_path, const std::string& name) {
    const std::string normalized_parent = normalize_path(parent_path);

    // 解析父目录
    const int32_t parent_inode = resolve_path(normalized_parent);
    if (parent_inode == -1) {
        return false;
    }

    // 检查目录是否已存在
    if (find_inode(parent_inode, name) != -1) {
        return false;
    }

    // 创建目录inode
    const int32_t dir_inode = create_inode(parent_inode, FS_DIRECTORY, name, 0);
    if (dir_inode == -1) {
        return false;
    }

    // 重新读取inode以确保创建成功
    INode inode;
    if(!read_inode(dir_inode, &inode)) {
        delete_inode(dir_inode);
        return false;
    }

    // 创建目录对象并初始化
    auto dir = std::make_unique<Directory>(dir_inode);
    dir->add_entry(".", dir_inode, FS_DIRECTORY);
    dir->add_entry("..", parent_inode, FS_DIRECTORY);

    // 保存目录内容
    if (!save_directory_content(dir_inode, *dir)) {
        delete_inode(dir_inode);
        return false;
    }

    // 缓存目录
    cache_directory(dir_inode, std::move(dir));

    return true;
}

bool INodeManager::read_file(const std::string& path, std::string& content) const
{
    const int32_t inode_id = resolve_path(path);
    if (inode_id == -1) {
        return false;
    }

    INode inode;
    if (!read_inode(inode_id, &inode) || inode.type != FS_FILE) {
        return false;
    }

    return read_file_data(inode_id, content);
}

bool INodeManager::write_file(const std::string& path, const std::string& content) const
{
    const int32_t inode_id = resolve_path(path);
    if (inode_id == -1) {
        return false;
    }

    INode inode;
    if (!read_inode(inode_id, &inode) || inode.type != FS_FILE) {
        return false;
    }

    // 调整文件大小
    if (!resize_inode(inode_id, content.size())) {
        return false;
    }

    return write_file_data(inode_id, content);
}

std::vector<FileInfo> INodeManager::list_directory(const std::string& normalized) const
{
    std::vector<FileInfo> result;

    const int32_t dir_inode = resolve_path(normalized);
    if (dir_inode == -1) {
        return result;
    }

    const std::shared_ptr<Directory> dir = get_directory(dir_inode);
    if (!dir) {
        return result;
    }

    const auto entries = dir->list_entries();
    for (const auto& entry : entries) {
        INode inode;
        if (read_inode(entry.inode_id, &inode)) {
            FileInfo info;
            info.name = entry.name;
            info.path = normalized + "/" + entry.name;
            info.is_directory = (inode.type == FS_DIRECTORY);
            info.size = inode.size;
            info.create_time = inode.create_time;
            info.modify_time = inode.modify_time;
            info.block_count = inode.block_count;
            info.start_block = inode.start_block;
            info.inode_id = inode.id;
            result.push_back(info);
        }
    }

    return result;
}

FileInfo INodeManager::get_file_info(const std::string& path) const
{
    FileInfo info;

    const int32_t inode_id = resolve_path(path);
    if (inode_id == -1) {
        return info;
    }

    INode inode;
    if (!read_inode(inode_id, &inode)) {
        return info;
    }

    info.name = inode.name;
    info.path = path;
    info.is_directory = (inode.type == FS_DIRECTORY);
    info.size = inode.size;
    info.create_time = inode.create_time;
    info.modify_time = inode.modify_time;
    info.block_count = inode.block_count;
    info.start_block = inode.start_block;
    info.inode_id = inode.id;

    return info;
}

// 私有辅助方法实现
std::shared_ptr<Directory> INodeManager::get_directory(uint32_t dir_id) const
{
    const auto it = directory_cache_.find(dir_id);
    if (it != directory_cache_.end()) {
        return std::shared_ptr<Directory>(it->second.get(), [](Directory*){});
    }

    // 从磁盘加载目录
    auto dir = std::make_shared<Directory>(dir_id);
    if (!load_directory_content(dir_id, *dir)) {
        return nullptr;
    }

    directory_cache_[dir_id] = dir;
    return dir;
}

bool INodeManager::load_directory_content(const uint32_t dir_id, Directory& dir) const
{
    INode inode;
    if (!read_inode(dir_id, &inode) || inode.type != FS_DIRECTORY) {
        return false;
    }

    if (inode.block_count == 0) {
        return true; // 空目录
    }

    // 读取目录数据
    std::vector<uint8_t> data(inode.size);
    for (uint32_t i = 0; i < inode.block_count; ++i) {
        std::vector<uint8_t> block_data(BLOCK_SIZE);
        if (!disk_->read_block(inode.start_block + i, block_data.data())) {
            return false;
        }

        const size_t copy_size = std::min(static_cast<size_t>(BLOCK_SIZE),
                                  data.size() - i * BLOCK_SIZE);
        memcpy(data.data() + i * BLOCK_SIZE, block_data.data(), copy_size);
    }

    return dir.deserialize(data);
}

bool INodeManager::save_directory_content(const uint32_t dir_id, const Directory& dir) const {

    // 获取目录的inode信息
    INode inode;
    if (!read_inode(dir_id, &inode)) {
        std::cerr << "Error: 无法读取目录inode" << std::endl;
        return false;
    }

    // 序列化目录内容
    std::vector<uint8_t> dir_data = dir.serialize();
    if (dir_data.empty()) {
        std::cerr << "Error: 目录序列化失败" << std::endl;
        return false;
    }


    // 确保数据块已分配
    if (inode.block_count == 0) {
        std::cerr << "Error: 目录数据块未正确分配" << std::endl;
        return false;
    }

    // 准备写入数据
    // Directories currently use only one block. inode.size is the size of dir_data.
    // The block should contain exactly dir_data.
    if (dir_data.size() > BLOCK_SIZE) {
        std::cerr << "Error: Directory content size (" << dir_data.size()
                  << " bytes) exceeds block size (" << BLOCK_SIZE << " bytes)." << std::endl;
        // This indicates a need for multi-block directory support or larger block_count for the directory.
        // For now, this is an error condition.
        return false;
    }

    std::vector<uint8_t> block_content(BLOCK_SIZE, 0); // Initialize with zeros
    std::memcpy(block_content.data(), dir_data.data(), dir_data.size());

    // 写入数据块
    if (!disk_->write_block(inode.start_block, block_content.data())) {
        std::cerr << "Error: 写入目录数据块失败" << std::endl;
        return false;
    }

    // 更新目录inode
    inode.size = dir_data.size();
    inode.modify_time = time(nullptr);

    if (!write_inode(dir_id, &inode)) {
        std::cerr << "Error: 更新目录inode失败" << std::endl;
        return false;
    }

    return true;
}

bool INodeManager::add_directory_entry(const uint32_t dir_id, const std::string& name,
                                      const uint32_t child_id, const uint8_t type) const
{
    const std::shared_ptr<Directory> dir = get_directory(dir_id);
    if (!dir) {
        return false;
    }

    if (!dir->add_entry(name, child_id, type)) {
        return false;
    }

    return save_directory_content(dir_id, *dir);
}

bool INodeManager::remove_directory_entry(const uint32_t dir_id, const std::string& name) const
{
    const std::shared_ptr<Directory> dir = get_directory(dir_id);
    if (!dir) {
        return false;
    }

    if (!dir->remove_entry(name)) {
        return false;
    }

    return save_directory_content(dir_id, *dir);
}

std::vector<std::string> INodeManager::split_path(const std::string& path) {
    std::vector<std::string> components;
    std::stringstream ss(path);
    std::string component;

    while (std::getline(ss, component, '/')) {
        if (component.empty() || component == ".") {
            // Skip empty parts (e.g., from '//') or current directory '.'
            continue;
        }
        if (component == "..") {
            // Go up one level
            if (!components.empty()) {
                components.pop_back();
            }
            // If components is empty, ".." at root level is still root.
        } else {
            components.push_back(component);
        }
    }

    return components;
}

std::string INodeManager::normalize_path(const std::string& path) {
    if (path.empty() || path[0] != '/') {
        return "/" + path;
    }

    std::string result = path;

    // 移除重复的斜杠
    for (size_t i = 0; i < result.length() - 1; ++i) {
        if (result[i] == '/' && result[i + 1] == '/') {
            result.erase(i + 1, 1);
            --i;
        }
    }

    // 移除末尾的斜杠（除非是根目录）
    if (result.length() > 1 && result.back() == '/') {
        result.pop_back();
    }

    return result;
}

bool INodeManager::is_valid_filename(const std::string& name) {
    if (name.empty() || name.length() > 63) {
        return false;
    }

    // 检查非法字符
    for (const char c : name) {
        if (c == '/' || c == '\0' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }

    return true;
}



bool INodeManager::delete_file(const std::string& path) {
    const int32_t inode_id = resolve_path(path);
    if (inode_id == -1) {
        return false;
    }

    INode inode;
    if (!read_inode(inode_id, &inode) || inode.type != FS_FILE) {
        return false;
    }

    return delete_inode(inode_id);
}

bool INodeManager::delete_directory(const std::string& path) {
    const int32_t inode_id = resolve_path(path);
    if (inode_id == -1) {
        return false;
    }

    INode inode;
    if (!read_inode(inode_id, &inode) || inode.type != FS_DIRECTORY) {
        return false;
    }

    // 不能删除根目录
    if (inode_id == ROOT_INODE_ID) {
        return false;
    }

    return delete_directory_recursive(inode_id);
}

bool INodeManager::read_file_data(const uint32_t inode_id, std::string& content) const
{
    INode inode;
    if (!read_inode(inode_id, &inode) || inode.type != FS_FILE) {
        return false;
    }
    // 使用细粒度锁保护
    LockGuard<SpinLock> inode_guard(*inode_locks_[inode_id]);

    // 使用vector作为中间缓冲区
    std::vector<uint8_t> buffer(inode.size);

    // 读取所有数据块
    for (uint32_t i = 0; i < inode.block_count; ++i) {
        std::vector<uint8_t> block_data(BLOCK_SIZE);
        if (!disk_->read_block(inode.start_block + i, block_data.data())) {
            return false;
        }

        const size_t offset = i * BLOCK_SIZE;
        const size_t remaining = inode.size - offset;
        const size_t copy_size = std::min(static_cast<size_t>(BLOCK_SIZE), remaining);

        if (copy_size > 0) {
            std::memcpy(buffer.data() + offset, block_data.data(), copy_size);
        }
    }

    // 将buffer转换为string
    content = std::string(buffer.begin(), buffer.end());
    return true;
}

bool INodeManager::write_file_data(const uint32_t inode_id, const std::string& content) const
{
    INode inode;
    if (!read_inode(inode_id, &inode) || inode.type != FS_FILE) {
        return false;
    }

    if (inode.size != content.size()) {
        if (!resize_inode(inode_id, content.size())) {
            return false;
        }
        // 重新读取调整大小后的inode
        if (!read_inode(inode_id, &inode)) {
            return false;
        }
    }

    // 写入所有数据块
    for (uint32_t i = 0; i < inode.block_count; ++i) {
        std::vector<uint8_t> block_data(BLOCK_SIZE, 0);
        const size_t copy_size = std::min(static_cast<size_t>(BLOCK_SIZE),
                                  content.size() - i * BLOCK_SIZE);

        if (copy_size > 0) {
            memcpy(block_data.data(), content.data() + i * BLOCK_SIZE, copy_size);
        }

        if (!disk_->write_block(inode.start_block + i, block_data.data())) {
            return false;
        }
    }

    // 更新修改时间
    inode.modify_time = time(nullptr);
    return write_inode(inode_id, &inode);
}

bool INodeManager::read_file_block(const std::string& path, const uint32_t block_index, std::string& content) const
{
    const int32_t inode_id = resolve_path(path);
    if (inode_id == -1) {
        return false;
    }

    return read_file_block_data(inode_id, block_index, content);
}

bool INodeManager::read_file_block_data(const uint32_t inode_id,const uint32_t block_index, std::string& content) const
{
    INode inode;
    if (!read_inode(inode_id, &inode) || inode.type != FS_FILE) {
        return false;
    }

    // 检查块索引是否有效
    if (block_index >= inode.block_count) {
        return false;
    }

    // 使用细粒度锁保护
    LockGuard<SpinLock> inode_guard(*inode_locks_[inode_id]);

    // 计算实际需要读取的字节数
    const size_t offset = block_index * BLOCK_SIZE;
    const size_t remaining = inode.size - offset;
    const size_t read_size = std::min(static_cast<size_t>(BLOCK_SIZE), remaining);

    // 读取数据块
    std::vector<uint8_t> block_data(BLOCK_SIZE);
    if (!disk_->read_block(inode.start_block + block_index, block_data.data())) {
        return false;
    }

    // 转换为string
    content = std::string(block_data.begin(), block_data.begin() + read_size);
    return true;
}

bool INodeManager::write_file_block(const std::string& path,const uint32_t block_index, const std::string& content) const
{
    const int32_t inode_id = resolve_path(path);
    if (inode_id == -1) {
        return false;
    }

    return write_file_block_data(inode_id, block_index, content);
}

bool INodeManager::write_file_block_data(const uint32_t inode_id, const uint32_t block_index, const std::string& content) const
{
    INode inode;
    if (!read_inode(inode_id, &inode) || inode.type != FS_FILE) {
        return false;
    }

    // 如果要写入的块索引超出当前文件大小，需要调整文件大小
    if (block_index >= inode.block_count) {
        const uint32_t new_size = (block_index + 1) * BLOCK_SIZE;
        if (!resize_inode(inode_id, new_size)) {
            return false;
        }
        // 重新读取调整大小后的inode
        if (!read_inode(inode_id, &inode)) {
            return false;
        }
    }

    std::vector<uint8_t> block_data(BLOCK_SIZE, 0);
    const size_t copy_size = std::min(static_cast<size_t>(BLOCK_SIZE), content.size());

    if (copy_size > 0) {
        memcpy(block_data.data(), content.data(), copy_size);
    }

    if (!disk_->write_block(inode.start_block + block_index, block_data.data())) {
        return false;
    }

    // 更新修改时间
    inode.modify_time = time(nullptr);
    return write_inode(inode_id, &inode);
}

bool INodeManager::directory_exists(const std::string& path) const
{
    const int32_t inode_id = resolve_path(path);
    if (inode_id == -1) {
        return false;
    }

    INode inode;
    if (!read_inode(inode_id, &inode)) {
        return false;
    }

    return inode.type == FS_DIRECTORY;
}

void INodeManager::cache_directory(const uint32_t dir_id, std::unique_ptr<Directory> dir) const {
    LockGuard<SimpleMutex> lock(cache_mutex_);
    directory_cache_[dir_id] = std::move(dir);
}

void INodeManager::remove_from_cache(const uint32_t dir_id) const
{
    LockGuard<SimpleMutex> lock(cache_mutex_);
    directory_cache_.erase(dir_id);
}

bool INodeManager::is_directory_empty(const uint32_t dir_id) const
{
    const std::shared_ptr<Directory> dir = get_directory(dir_id);
    if (!dir) {
        return true; // 如果找不到目录，视为空
    }

    return dir->is_empty();
}

bool INodeManager::delete_directory_recursive(const uint32_t dir_id) {
    if (dir_id == ROOT_INODE_ID) {
        return false; // 不能删除根目录
    }

    const std::shared_ptr<Directory> dir = get_directory(dir_id);
    if (!dir) {
        return false;
    }

    // 获取目录中所有条目
    const auto entries = dir->list_entries();

    // 递归删除所有子目录和文件
    for (const auto& entry : entries) {
        if (entry.name == "." || entry.name == "..") {
            continue; // 跳过当前目录和父目录
        }

        INode child_inode;
        if (!read_inode(entry.inode_id, &child_inode)) {
            continue;
        }

        if (child_inode.type == FS_DIRECTORY) {
            if (!delete_directory_recursive(entry.inode_id)) {
                return false;
            }
        } else {
            if (!delete_inode(entry.inode_id)) {
                return false;
            }
        }
    }

    // 删除目录本身
    return delete_inode(dir_id);
}