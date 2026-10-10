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

// An open in-memory file: the filesystem's node, read and written through
// InMemoryFileSystem::ReadAt/WriteAt. Holding a shared_ptr on the filesystem,
// it keeps working after the filesystem is unmounted or the last outside
// reference to it is gone. The access mode it was opened with is enforced on
// every call: Write on a read-only descriptor and Read on a write-only one
// fail with kIOError, as EBADF would.
class InMemoryFileDescriptor final : public IFileDescriptor {
public:
    static std::shared_ptr<InMemoryFileDescriptor> Create(
        std::shared_ptr<InMemoryFileSystem> fs, std::string normalizedPath, int flags) {
        return std::shared_ptr<InMemoryFileDescriptor>(
            new InMemoryFileDescriptor(std::move(fs), std::move(normalizedPath), flags));
    }

    ssize_t Read(void* buf, size_t count) override {
        if (!m_readable) {
            return kIOError;
        }
        return m_fs->ReadAt(m_path, m_position, buf, count);
    }

    ssize_t Write(const void* buf, size_t count) override {
        if (!m_writable) {
            return kIOError;
        }
        return m_fs->WriteAt(m_path, m_position, m_append, buf, count);
    }

    bool IsTerminal() const override { return false; }

private:
    InMemoryFileDescriptor(std::shared_ptr<InMemoryFileSystem> fs, std::string normalizedPath, int flags)
        : m_fs(std::move(fs))
        , m_path(std::move(normalizedPath))
        , m_writable((flags & (kFileWriteOnlyBit | kFileReadWriteBit)) != 0)
        , m_readable((flags & kFileWriteOnlyBit) == 0)
        , m_append((flags & kFileAppendBit) != 0)
    {
    }

    std::shared_ptr<InMemoryFileSystem> m_fs;
    const std::string m_path;
    // Touched only inside ReadAt/WriteAt, under the filesystem's mutex.
    size_t m_position = 0;
    const bool m_writable;
    const bool m_readable;
    const bool m_append;
};

std::shared_ptr<InMemoryFileSystem> InMemoryFileSystem::Create() {
    return std::shared_ptr<InMemoryFileSystem>(new InMemoryFileSystem());
}

InMemoryFileSystem::InMemoryFileSystem() {
    m_nodes["/"] = Node::Make(true);
}

void InMemoryFileSystem::TouchDirectory(const std::string& normalizedPath) {
    auto it = m_nodes.find(normalizedPath);
    if (it != m_nodes.end()) {
        it->second.Modified();
    }
}

InMemoryFileSystem::~InMemoryFileSystem() = default;

std::shared_ptr<IFileDescriptor> InMemoryFileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    return LocalOpenFile(pathname, flags, 0);
}

std::shared_ptr<IFileDescriptor> InMemoryFileSystem::LocalOpenFile(const std::string& pathname, int flags, int /*mode*/) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string normalized = NormalizeVirtualPath(pathname);

    auto it = m_nodes.find(normalized);
    bool exists = it != m_nodes.end();
    if (exists && it->second.isDirectory) {
        return nullptr;
    }

    if (!exists) {
        if ((flags & kFileCreateBit) == 0) {
            return nullptr;
        }
        auto parentIt = m_nodes.find(ParentOf(normalized));
        if (parentIt == m_nodes.end() || !parentIt->second.isDirectory) {
            return nullptr;
        }
        it = m_nodes.emplace(normalized, Node::Make(false)).first;
        TouchDirectory(ParentOf(normalized));
    } else if (flags & kFileTruncateBit) {
        it->second.data.clear();
        it->second.Modified();
    }

    // The position starts at 0, with append taken into account or not at every
    // write instead; reads of an O_RDWR|O_APPEND descriptor start at 0, as on
    // Linux.
    return InMemoryFileDescriptor::Create(shared_from_this(), normalized, flags);
}

ssize_t InMemoryFileSystem::ReadAt(const std::string& normalizedPath, size_t& position, void* buf, size_t count) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto nodeIt = m_nodes.find(normalizedPath);
    if (nodeIt == m_nodes.end()) {
        return kIOError;
    }
    const auto& data = nodeIt->second.data;
    size_t& pos = position;
    if (pos >= data.size()) {
        return 0;
    }
    size_t toCopy = std::min(count, data.size() - pos);
    std::memcpy(buf, data.data() + pos, toCopy);
    pos += toCopy;
    nodeIt->second.accessTime = CurrentFileDateTime();
    return static_cast<ssize_t>(toCopy);
}

ssize_t InMemoryFileSystem::WriteAt(const std::string& normalizedPath, size_t& position, bool append, const void* buf, size_t count) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto nodeIt = m_nodes.find(normalizedPath);
    if (nodeIt == m_nodes.end()) {
        return kIOError;
    }
    auto& data = nodeIt->second.data;
    size_t& pos = position;
    // As O_APPEND demands: the write goes to the end of the file as it is now,
    // not as it was when the file was opened -- two ">> log" writers no longer
    // overwrite each other.
    if (append) {
        pos = data.size();
    }
    if (count > SIZE_MAX - pos) {
        return kIOError; // pos + count would overflow
    }
    if (pos + count > data.size()) {
        data.resize(pos + count);
    }
    std::memcpy(data.data() + pos, buf, count);
    pos += count;
    nodeIt->second.Modified();
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
    m_nodes.emplace(normalized, Node::Make(true));
    TouchDirectory(ParentOf(normalized));
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
    TouchDirectory(ParentOf(normalized));
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
    TouchDirectory(ParentOf(normalized));
    return 0;
}

int InMemoryFileSystem::LocalRename(const std::string& oldPath, const std::string& newPath) {
    // An open descriptor refers to its file by path, not by identity (see
    // InMemoryFileDescriptor), so it follows the path: after a rename a
    // descriptor opened on the old path finds nothing (kIOError), as one on a
    // removed file already does, and one opened on the replaced target reads
    // the moved file. That is the one difference from POSIX, where a
    // descriptor follows its file across a rename.
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string from = NormalizeVirtualPath(oldPath);
    const std::string to = NormalizeVirtualPath(newPath);
    if (from == "/" || to == "/" || m_nodes.find(from) == m_nodes.end()) {
        return kFileSystemError;
    }
    if (from == to) {
        return 0;
    }
    // A directory cannot move into itself or below itself.
    if (to.compare(0, from.size() + 1, from + "/") == 0) {
        return kFileSystemError;
    }
    const std::string newParent = ParentOf(to);
    const auto parentIt = m_nodes.find(newParent);
    if (parentIt == m_nodes.end() || !parentIt->second.isDirectory) {
        return kFileSystemError;
    }
    const auto newIt = m_nodes.find(to);
    if (newIt != m_nodes.end()) {
        if (newIt->second.isDirectory != m_nodes.find(from)->second.isDirectory) {
            return kFileSystemError; // a file would replace a directory, or a directory a file
        }
        if (newIt->second.isDirectory) {
            const std::string prefix = to + "/";
            for (const auto& entry : m_nodes) {
                if (entry.first.compare(0, prefix.size(), prefix) == 0) {
                    return kFileSystemError; // a directory is replaced only when empty
                }
            }
        }
        m_nodes.erase(newIt);
    }
    // Re-key the node and, for a directory, everything below it: the keys are
    // collected first and then each node is extracted and re-inserted, so the
    // map is never modified while being iterated.
    std::vector<std::string> keys;
    keys.push_back(from);
    const std::string prefix = from + "/";
    for (const auto& entry : m_nodes) {
        if (entry.first.compare(0, prefix.size(), prefix) == 0) {
            keys.push_back(entry.first);
        }
    }
    for (const auto& key : keys) {
        auto handle = m_nodes.extract(key);
        handle.key() = (key == from) ? to : to + key.substr(from.size());
        if (key == from) {
            // rename() updates ctime, not the times of what is inside.
            handle.mapped().changeTime = CurrentFileDateTime();
        }
        m_nodes.insert(std::move(handle));
    }
    TouchDirectory(ParentOf(from));
    if (newParent != ParentOf(from)) {
        TouchDirectory(newParent);
    }
    return 0;
}

int InMemoryFileSystem::LocalSetTimes(const std::string& path,
                                     const std::optional<FileDateTime>& accessTime,
                                     const std::optional<FileDateTime>& modificationTime) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string normalized = NormalizeVirtualPath(path);
    auto it = m_nodes.find(normalized);
    if (it == m_nodes.end()) {
        return kFileSystemError;
    }
    if (accessTime) {
        it->second.accessTime = *accessTime;
    }
    if (modificationTime) {
        it->second.modificationTime = *modificationTime;
    }
    // utimensat() sets ctime unless nothing is set at all.
    if (accessTime || modificationTime) {
        it->second.changeTime = CurrentFileDateTime();
    }
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

int InMemoryFileSystem::LocalStat(const std::string& path, FileStatus& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string normalized = NormalizeVirtualPath(path);
    auto it = m_nodes.find(normalized);
    if (it == m_nodes.end()) {
        return -1;
    }
    const Node& node = it->second;
    out.type = node.isDirectory ? DirectoryEntryType::Dir : DirectoryEntryType::File;
    out.size = node.isDirectory ? 0 : static_cast<uint64_t>(node.data.size());
    out.blocks = BlocksForSize(out.size);
    out.linkCount = 1;
    if (node.isDirectory) {
        // Its own "." and the entry naming it, plus each subdirectory's "..".
        out.linkCount = 2;
        for (const auto& entry : m_nodes) {
            if (entry.second.isDirectory && entry.first != normalized && entry.first != "/" &&
                ParentOf(entry.first) == normalized) {
                ++out.linkCount;
            }
        }
    }
    out.accessTime = node.accessTime;
    out.modificationTime = node.modificationTime;
    out.changeTime = node.changeTime;
    return 0;
}

std::string InMemoryFileSystem::AbsolutePathFor(const std::string& path) const {
    return NormalizeVirtualPath(path);
}

}
