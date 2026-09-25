#include "DeviceFileSystem.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include "FilesystemUtils.h"
#include "VirtualPath.h"

namespace Haisos {

const DeviceFileSystem::DeviceInfo DeviceFileSystem::kDevices[] = {
    {"null", Device::Null, 1, 3},
    {"zero", Device::Zero, 1, 5},
};

std::shared_ptr<DeviceFileSystem> DeviceFileSystem::Create() {
    return std::shared_ptr<DeviceFileSystem>(new DeviceFileSystem());
}

DeviceFileSystem::DeviceFileSystem() : m_createdTime(CurrentFileDateTime()) {}
DeviceFileSystem::~DeviceFileSystem() = default;

const DeviceFileSystem::DeviceInfo* DeviceFileSystem::DeviceAt(const std::string& normalizedPath) {
    for (const auto& info : kDevices) {
        if (normalizedPath == std::string("/") + info.name) {
            return &info;
        }
    }
    return nullptr;
}

int DeviceFileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    return LocalOpenFile(pathname, flags, 0);
}

int DeviceFileSystem::LocalOpenFile(const std::string& pathname, int /*flags*/, int /*mode*/) {
    // Only a device opens: the root is a directory, and anything else would
    // have to be created. A device takes any flags -- O_CREAT finds it already
    // there, and O_TRUNC has nothing to truncate.
    const DeviceInfo* info = DeviceAt(NormalizeVirtualPath(pathname));
    if (!info) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const int fd = m_nextFd++;
    m_openHandles[fd] = info->device;
    return fd;
}

int DeviceFileSystem::LocalCloseFile(int fd) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_openHandles.erase(fd) > 0 ? 0 : -1;
}

ssize_t DeviceFileSystem::LocalReadFile(int fd, void* buf, size_t count) {
    Device device;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_openHandles.find(fd);
        if (it == m_openHandles.end()) {
            return -1;
        }
        device = it->second;
    }
    if (device == Device::Null) {
        return 0;
    }
    // As read() itself, never more than a ssize_t can report.
    const size_t filled = std::min(count, static_cast<size_t>(std::numeric_limits<ssize_t>::max()));
    std::memset(buf, 0, filled);
    return static_cast<ssize_t>(filled);
}

ssize_t DeviceFileSystem::LocalWriteFile(int fd, const void* /*buf*/, size_t count) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_openHandles.count(fd) == 0) {
        return -1;
    }
    // Both devices take everything, and keep none of it.
    return static_cast<ssize_t>(std::min(count, static_cast<size_t>(std::numeric_limits<ssize_t>::max())));
}

int DeviceFileSystem::LocalCreateDirectory(const std::string& /*pathname*/, int /*mode*/) {
    return -1;
}

int DeviceFileSystem::LocalRemoveDirectory(const std::string& /*pathname*/) {
    return -1;
}

int DeviceFileSystem::LocalRemoveFile(const std::string& /*pathname*/) {
    return -1;
}

std::vector<DirectoryEntry> DeviceFileSystem::LocalReadDirectory(const std::string& path) {
    std::vector<DirectoryEntry> entries;
    if (NormalizeVirtualPath(path) != "/") {
        return entries;
    }
    for (const auto& info : kDevices) {
        entries.push_back(DirectoryEntry{info.name, DirectoryEntryType::CharDevice});
    }
    return entries;
}

int DeviceFileSystem::LocalStat(const std::string& path, FileStatus& out) {
    const std::string normalized = NormalizeVirtualPath(path);
    FileStatus status;
    status.accessTime = status.modificationTime = status.changeTime = m_createdTime;
    if (normalized == "/") {
        status.type = DirectoryEntryType::Dir;
        status.linkCount = 2; // its own "." and the entry naming it; no subdirectories
    } else if (const DeviceInfo* info = DeviceAt(normalized)) {
        status.type = DirectoryEntryType::CharDevice;
        status.deviceMajor = info->major;
        status.deviceMinor = info->minor;
    } else {
        return -1;
    }
    // A device stores nothing: size and blocks stay 0, as stat() reports them.
    out = status;
    return 0;
}

std::string DeviceFileSystem::AbsolutePathFor(const std::string& path) const {
    return NormalizeVirtualPath(path);
}

}
