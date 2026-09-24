#include "InMemoryFileSystem.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "FilesystemUtils.h"
#include "VirtualPath.h"

namespace Haisos {

namespace {

std::string ParentOf(const std::string& normalizedPath) {
    auto pos = normalizedPath.find_last_of('/');
    if (pos == std::string::npos || pos == 0) {
        return "/";
    }
    return normalizedPath.substr(0, pos);
}

std::string LastSegment(const std::string& normalizedPath) {
    auto pos = normalizedPath.find_last_of('/');
    return (pos == std::string::npos) ? normalizedPath : normalizedPath.substr(pos + 1);
}

} // namespace

std::shared_ptr<InMemoryFileSystem> InMemoryFileSystem::Create() {
    return std::shared_ptr<InMemoryFileSystem>(new InMemoryFileSystem());
}

InMemoryFileSystem::InMemoryFileSystem() {
    m_nodes["/"] = Node{true, {}};
}

InMemoryFileSystem::~InMemoryFileSystem() = default;

int InMemoryFileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    return LocalOpenFile(pathname, flags, 0);
}

int InMemoryFileSystem::LocalOpenFile(const std::string& pathname, int flags, int /*mode*/) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string normalized = NormalizeVirtualPath(pathname);

    auto it = m_nodes.find(normalized);
    bool exists = it != m_nodes.end();
    if (exists && it->second.isDirectory) {
        return -1;
    }

    if (!exists) {
        if ((flags & kFileCreateBit) == 0) {
            return -1;
        }
        auto parentIt = m_nodes.find(ParentOf(normalized));
        if (parentIt == m_nodes.end() || !parentIt->second.isDirectory) {
            return -1;
        }
        it = m_nodes.emplace(normalized, Node{false, {}}).first;
    } else if (flags & kFileTruncateBit) {
        it->second.data.clear();
    }

    int fd = m_nextFd++;
    size_t startPos = (flags & kFileAppendBit) ? it->second.data.size() : 0;
    m_openHandles[fd] = OpenHandle{normalized, startPos};
    return fd;
}

int InMemoryFileSystem::LocalCloseFile(int fd) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_openHandles.erase(fd) > 0 ? 0 : -1;
}

ssize_t InMemoryFileSystem::LocalReadFile(int fd, void* buf, size_t count) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto handleIt = m_openHandles.find(fd);
    if (handleIt == m_openHandles.end()) {
        return -1;
    }
    auto nodeIt = m_nodes.find(handleIt->second.path);
    if (nodeIt == m_nodes.end()) {
        return -1;
    }
    const auto& data = nodeIt->second.data;
    size_t& pos = handleIt->second.position;
    if (pos >= data.size()) {
        return 0;
    }
    size_t toCopy = std::min(count, data.size() - pos);
    std::memcpy(buf, data.data() + pos, toCopy);
    pos += toCopy;
    return static_cast<ssize_t>(toCopy);
}

ssize_t InMemoryFileSystem::LocalWriteFile(int fd, const void* buf, size_t count) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto handleIt = m_openHandles.find(fd);
    if (handleIt == m_openHandles.end()) {
        return -1;
    }
    auto nodeIt = m_nodes.find(handleIt->second.path);
    if (nodeIt == m_nodes.end()) {
        return -1;
    }
    auto& data = nodeIt->second.data;
    size_t& pos = handleIt->second.position;
    if (count > SIZE_MAX - pos) {
        return -1; // pos + count would overflow
    }
    if (pos + count > data.size()) {
        data.resize(pos + count);
    }
    std::memcpy(data.data() + pos, buf, count);
    pos += count;
    return static_cast<ssize_t>(count);
}

int InMemoryFileSystem::LocalCreateDirectory(const std::string& pathname, int /*mode*/) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string normalized = NormalizeVirtualPath(pathname);
    if (m_nodes.count(normalized) > 0) {
        return -1;
    }
    auto parentIt = m_nodes.find(ParentOf(normalized));
    if (parentIt == m_nodes.end() || !parentIt->second.isDirectory) {
        return -1;
    }
    m_nodes.emplace(normalized, Node{true, {}});
    return 0;
}

int InMemoryFileSystem::LocalRemoveDirectory(const std::string& pathname) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string normalized = NormalizeVirtualPath(pathname);
    auto it = m_nodes.find(normalized);
    if (it == m_nodes.end() || !it->second.isDirectory || normalized == "/") {
        return -1;
    }
    std::string prefix = normalized + "/";
    for (const auto& entry : m_nodes) {
        if (entry.first.compare(0, prefix.size(), prefix) == 0) {
            return -1; // not empty
        }
    }
    m_nodes.erase(it);
    return 0;
}

int InMemoryFileSystem::LocalRemoveFile(const std::string& pathname) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string normalized = NormalizeVirtualPath(pathname);
    auto it = m_nodes.find(normalized);
    if (it == m_nodes.end() || it->second.isDirectory) {
        return -1;
    }
    // As with unlink(), a handle still open on the file is not closed by this;
    // it just finds nothing left behind it, the way reads and writes on any
    // handle whose node is gone already do.
    m_nodes.erase(it);
    return 0;
}

std::vector<DirectoryEntry> InMemoryFileSystem::LocalReadDirectory(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string normalized = NormalizeVirtualPath(path);
    auto it = m_nodes.find(normalized);
    if (it == m_nodes.end() || !it->second.isDirectory) {
        return {};
    }

    std::vector<DirectoryEntry> entries;
    for (const auto& entry : m_nodes) {
        if (entry.first == normalized) {
            continue;
        }
        if (ParentOf(entry.first) == normalized) {
            entries.push_back(DirectoryEntry{LastSegment(entry.first), entry.second.isDirectory ? DirectoryEntryType::Dir : DirectoryEntryType::File});
        }
    }
    return entries;
}


std::string InMemoryFileSystem::AbsolutePathFor(const std::string& path) const {
    return NormalizeVirtualPath(path);
}

}
