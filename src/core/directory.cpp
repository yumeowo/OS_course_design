#include "directory.h"
#include <cstring>
#include <algorithm>

Directory::Directory(const uint32_t dir_inode_id) : dir_inode_id_(dir_inode_id) {}

bool Directory::add_entry(const std::string& name, const uint32_t inode_id, const uint8_t type) {
    ReadWriteLock::WriteGuard lock(rw_lock_);

    // 检查名称长度
    if (name.empty() || name.length() >= sizeof(DirectoryEntry::name)) {
        return false;
    }

    // 检查是否已存在
    DirectoryEntry entry;
    if (find_entry(name, entry)) {
        return false;
    }

    // 检查目录项数量限制
    if (entries_.size() >= MAX_ENTRIES) {
        return false;
    }

    // 创建新目录项
    DirectoryEntry new_entry;
    new_entry.inode_id = inode_id;
    new_entry.type = type;
    std::strncpy(new_entry.name, name.c_str(), sizeof(new_entry.name) - 1);
    new_entry.name[sizeof(new_entry.name) - 1] = '\0';

    entries_.push_back(new_entry);
    return true;
}

bool Directory::remove_entry(const std::string& name) {
    ReadWriteLock::WriteGuard lock(rw_lock_);

    const auto it = std::find_if(entries_.begin(), entries_.end(),
                          [&name](const DirectoryEntry& entry) {
                              return std::strcmp(entry.name, name.c_str()) == 0;
                          });

    if (it == entries_.end()) {
        return false;
    }

    entries_.erase(it);
    return true;
}

bool Directory::find_entry(const std::string& name, DirectoryEntry& entry) {
    ReadWriteLock::ReadGuard lock(rw_lock_);

    const auto it = std::find_if(entries_.begin(), entries_.end(),
                          [&name](const DirectoryEntry& e) {
                              return std::strcmp(e.name, name.c_str()) == 0;
                          });

    if (it == entries_.end()) {
        return false;
    }

    entry = *it;
    return true;
}

std::vector<DirectoryEntry> Directory::list_entries() const {
    ReadWriteLock::ReadGuard lock(rw_lock_);

    return entries_;
}

bool Directory::is_empty() const {
    ReadWriteLock::ReadGuard lock(rw_lock_);

    return entries_.empty();
}

size_t Directory::get_entry_count() const {
    ReadWriteLock::ReadGuard lock(rw_lock_);

    return entries_.size();
}

uint32_t Directory::get_inode_id() const {
    return dir_inode_id_;
}

std::vector<uint8_t> Directory::serialize() const {
    ReadWriteLock::ReadGuard lock(rw_lock_);

    std::vector<uint8_t> data;
    const size_t total_size = sizeof(uint32_t) + entries_.size() * sizeof(DirectoryEntry);
    data.resize(total_size);

    // 写入目录项数量
    const uint32_t count = static_cast<uint32_t>(entries_.size());
    std::memcpy(data.data(), &count, sizeof(count));

    // 写入目录项数据
    if (!entries_.empty()) {
        std::memcpy(data.data() + sizeof(count), entries_.data(),
                   entries_.size() * sizeof(DirectoryEntry));
    }

    return data;
}

bool Directory::deserialize(const std::vector<uint8_t>& data) {
    ReadWriteLock::WriteGuard lock(rw_lock_);  // 使用写锁

    if (data.size() < sizeof(uint32_t)) {
        return false;
    }

    // 读取目录项数量
    uint32_t count;
    std::memcpy(&count, data.data(), sizeof(count));

    // 验证数据大小
    const size_t expected_size = sizeof(uint32_t) + count * sizeof(DirectoryEntry);
    if (data.size() != expected_size || count > MAX_ENTRIES) {
        return false;
    }

    // 清空现有数据
    entries_.clear();

    // 读取目录项
    if (count > 0) {
        entries_.resize(count);
        std::memcpy(entries_.data(), data.data() + sizeof(count),
                   count * sizeof(DirectoryEntry));
    }

    return true;
}

bool Directory::validate() const {
    ReadWriteLock::ReadGuard lock(rw_lock_);

    // 检查目录项数量限制
    if (entries_.size() > MAX_ENTRIES) {
        return false;
    }

    // 检查目录项名称唯一性
    for (size_t i = 0; i < entries_.size(); i++) {
        for (size_t j = i + 1; j < entries_.size(); j++) {
            if (std::strcmp(entries_[i].name, entries_[j].name) == 0) {
                return false;
            }
        }
    }

    return true;
}