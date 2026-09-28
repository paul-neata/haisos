#include "MountableFileSystem.h"
#include <algorithm>
#include <cstring>
#include "BuiltinCommandFile.h"
#include "FilesystemUtils.h"
#include "VirtualPath.h"

namespace Haisos {

namespace {

// "/a/b" is at or under "/a/b" and "/", but not under "/a/bc".
bool IsAtOrUnder(const std::string& path, const std::string& directory) {
    if (directory == "/" || path == directory) {
        return true;
    }
    return path.size() > directory.size() &&
        path.compare(0, directory.size(), directory) == 0 &&
        path[directory.size()] == '/';
}

} // namespace

void MountableFileSystem::Mount(const std::string& whereToMount, std::shared_ptr<IFileSystem> toBeMounted) {
    m_mounts.Mount(AbsolutePathFor(whereToMount), std::move(toBeMounted));
}

void MountableFileSystem::Unmount(const std::string& mountedPath) {
    m_mounts.Unmount(AbsolutePathFor(mountedPath));
}

int MountableFileSystem::OpenFile(const std::string& pathname, int flags) {
    const std::string absolute = AbsolutePathFor(pathname);
    int builtinFd = -1;
    if (OpenOwnBuiltin(absolute, flags, builtinFd)) {
        return builtinFd;
    }
    auto route = m_mounts.Resolve(absolute);
    if (route.filesystem) {
        return m_mounts.RegisterFd(route.filesystem, route.filesystem->OpenFile(route.innerPath, flags));
    }
    return LocalOpenFile(pathname, flags);
}

int MountableFileSystem::OpenFile(const std::string& pathname, int flags, int mode) {
    const std::string absolute = AbsolutePathFor(pathname);
    int builtinFd = -1;
    if (OpenOwnBuiltin(absolute, flags, builtinFd)) {
        return builtinFd;
    }
    auto route = m_mounts.Resolve(absolute);
    if (route.filesystem) {
        return m_mounts.RegisterFd(route.filesystem, route.filesystem->OpenFile(route.innerPath, flags, mode));
    }
    return LocalOpenFile(pathname, flags, mode);
}

bool MountableFileSystem::OpenOwnBuiltin(const std::string& absolute, int flags, int& outFd) {
    // A builtin of this filesystem's own shadows whatever is beneath it, a
    // mount included. It is read-only: its text is all there is to it.
    auto builtin = OwnBuiltinAt(absolute);
    if (!builtin) {
        return false;
    }
    if (RequestsWriteAccess(flags)) {
        outFd = -1;
        return true;
    }
    outFd = MountPoints::AllocateSyntheticFd();
    std::lock_guard<std::mutex> lock(m_builtinsMutex);
    m_builtinFiles[outFd] = BuiltinFileHandle{BuiltinCommandFileContent(*builtin), 0};
    return true;
}

int MountableFileSystem::CloseFile(int fd) {
    if (MountPoints::IsSynthetic(fd)) {
        {
            std::lock_guard<std::mutex> lock(m_builtinsMutex);
            if (m_builtinFiles.erase(fd) > 0) {
                return 0;
            }
        }
        IFileSystem* filesystem = nullptr;
        int innerFd = -1;
        if (m_mounts.LookupFd(fd, filesystem, innerFd)) {
            const int result = filesystem->CloseFile(innerFd);
            m_mounts.ReleaseFd(fd);
            return result;
        }
    }
    return LocalCloseFile(fd);
}

ssize_t MountableFileSystem::ReadFile(int fd, void* buf, size_t count) {
    if (MountPoints::IsSynthetic(fd)) {
        {
            std::lock_guard<std::mutex> lock(m_builtinsMutex);
            auto it = m_builtinFiles.find(fd);
            if (it != m_builtinFiles.end()) {
                BuiltinFileHandle& handle = it->second;
                const size_t toCopy = std::min(count, handle.content.size() - handle.position);
                std::memcpy(buf, handle.content.data() + handle.position, toCopy);
                handle.position += toCopy;
                return static_cast<ssize_t>(toCopy);
            }
        }
        IFileSystem* filesystem = nullptr;
        int innerFd = -1;
        if (m_mounts.LookupFd(fd, filesystem, innerFd)) {
            return filesystem->ReadFile(innerFd, buf, count);
        }
    }
    return LocalReadFile(fd, buf, count);
}

ssize_t MountableFileSystem::WriteFile(int fd, const void* buf, size_t count) {
    if (MountPoints::IsSynthetic(fd)) {
        {
            std::lock_guard<std::mutex> lock(m_builtinsMutex);
            if (m_builtinFiles.count(fd) > 0) {
                return -1;
            }
        }
        IFileSystem* filesystem = nullptr;
        int innerFd = -1;
        if (m_mounts.LookupFd(fd, filesystem, innerFd)) {
            return filesystem->WriteFile(innerFd, buf, count);
        }
    }
    return LocalWriteFile(fd, buf, count);
}

int MountableFileSystem::CreateDirectory(const std::string& pathname, int mode) {
    const std::string absolute = AbsolutePathFor(pathname);
    if (OwnBuiltinAt(absolute)) {
        return -1;
    }
    auto route = m_mounts.Resolve(absolute);
    if (route.filesystem) {
        return route.filesystem->CreateDirectory(route.innerPath, mode);
    }
    return LocalCreateDirectory(pathname, mode);
}

int MountableFileSystem::RemoveDirectory(const std::string& pathname) {
    const std::string absolute = AbsolutePathFor(pathname);
    // Checked before routing: the directory a builtin of this filesystem sits
    // in may well be served by a mount, and that mount knows nothing of it.
    if (HasOwnBuiltinAtOrUnder(absolute)) {
        return -1;
    }
    auto route = m_mounts.Resolve(absolute);
    if (route.filesystem) {
        return route.filesystem->RemoveDirectory(route.innerPath);
    }
    return LocalRemoveDirectory(pathname);
}

int MountableFileSystem::RemoveFile(const std::string& pathname) {
    const std::string absolute = AbsolutePathFor(pathname);
    // A builtin goes only through RemoveBuiltinCommand.
    if (OwnBuiltinAt(absolute)) {
        return -1;
    }
    auto route = m_mounts.Resolve(absolute);
    if (route.filesystem) {
        return route.filesystem->RemoveFile(route.innerPath);
    }
    return LocalRemoveFile(pathname);
}

std::vector<DirectoryEntry> MountableFileSystem::ReadDirectory(const std::string& path) {
    const std::string absolute = AbsolutePathFor(path);
    std::vector<DirectoryEntry> entries;
    auto route = m_mounts.Resolve(absolute);
    if (route.filesystem) {
        entries = route.filesystem->ReadDirectory(route.innerPath);
    } else {
        entries = LocalReadDirectory(path);
        // A mount point does not have to exist underneath, so synthesize the next
        // segment towards each one; otherwise a mount would be unreachable by
        // listing down from the root.
        for (const auto& segment : m_mounts.ChildSegments(absolute)) {
            const bool alreadyListed = std::any_of(entries.begin(), entries.end(),
                [&segment](const DirectoryEntry& entry) { return entry.name == segment; });
            if (!alreadyListed) {
                entries.push_back(DirectoryEntry{segment, DirectoryEntryType::Dir});
            }
        }
    }
    AddOwnBuiltinEntries(absolute, entries);
    PutDotEntriesFirst(path, entries);
    return entries;
}

void MountableFileSystem::PutDotEntriesFirst(const std::string& path, std::vector<DirectoryEntry>& entries) {
    // A filesystem this one wraps, or has mounted, has put them in
    // already; they are put back first, once.
    const auto dots = std::remove_if(entries.begin(), entries.end(),
        [](const DirectoryEntry& entry) { return entry.name == "." || entry.name == ".."; });
    const bool hadDots = dots != entries.end();
    entries.erase(dots, entries.end());
    // Only a directory lists anything, so an empty listing is the one case
    // that may be of no directory at all.
    FileStatus status;
    if (!hadDots && entries.empty() && (Stat(path, status) != 0 || status.type != DirectoryEntryType::Dir)) {
        return;
    }
    entries.insert(entries.begin(), {DirectoryEntry{".", DirectoryEntryType::Dir}, DirectoryEntry{"..", DirectoryEntryType::Dir}});
}

int MountableFileSystem::Stat(const std::string& path, FileStatus& out) {
    const std::string absolute = AbsolutePathFor(path);
    {
        std::lock_guard<std::mutex> lock(m_builtinsMutex);
        auto it = m_builtins.find(absolute);
        if (it != m_builtins.end()) {
            FileStatus status;
            status.type = DirectoryEntryType::File;
            status.size = BuiltinCommandFileContent(it->second.name).size();
            // Compiled into Haisos, it takes no storage on any filesystem.
            status.blocks = 0;
            status.linkCount = 1;
            status.accessTime = status.modificationTime = status.changeTime = it->second.placedTime;
            out = status;
            return 0;
        }
    }
    auto route = m_mounts.Resolve(absolute);
    if (route.filesystem) {
        return route.filesystem->Stat(route.innerPath, out);
    }
    if (LocalStat(path, out) == 0) {
        return 0;
    }
    // A directory on the way down to a mount point exists as far as anyone
    // listing it can tell, even with nothing underneath (see ReadDirectory).
    if (auto mountedAt = m_mounts.LatestMountTimeBelow(absolute)) {
        FileStatus status;
        status.type = DirectoryEntryType::Dir;
        status.linkCount = 2 + m_mounts.ChildSegments(absolute).size();
        status.accessTime = status.modificationTime = status.changeTime = *mountedAt;
        out = status;
        return 0;
    }
    return -1;
}

int MountableFileSystem::AddBuiltinCommand(const std::string& path, const std::string& builtinName) {
    const std::string absolute = AbsolutePathFor(path);
    if (absolute == "/" || builtinName.empty() || !LocalCanHoldBuiltinCommands()) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(m_builtinsMutex);
    return m_builtins.emplace(absolute, Builtin{builtinName, CurrentFileDateTime()}).second ? 0 : -1;
}

int MountableFileSystem::RemoveBuiltinCommand(const std::string& path) {
    const std::string absolute = AbsolutePathFor(path);
    std::lock_guard<std::mutex> lock(m_builtinsMutex);
    // A file of it still open keeps reading its text until closed, the way an
    // unlinked file does.
    return m_builtins.erase(absolute) > 0 ? 0 : -1;
}

std::optional<std::string> MountableFileSystem::IsBuiltinCommand(const std::string& path) {
    const std::string absolute = AbsolutePathFor(path);
    if (auto own = OwnBuiltinAt(absolute)) {
        return own;
    }
    auto route = m_mounts.Resolve(absolute);
    if (route.filesystem) {
        return route.filesystem->IsBuiltinCommand(route.innerPath);
    }
    return LocalIsBuiltinCommand(path);
}

std::optional<std::string> MountableFileSystem::OwnBuiltinAt(const std::string& absolute) const {
    std::lock_guard<std::mutex> lock(m_builtinsMutex);
    auto it = m_builtins.find(absolute);
    if (it == m_builtins.end()) {
        return std::nullopt;
    }
    return it->second.name;
}

bool MountableFileSystem::HasOwnBuiltinAtOrUnder(const std::string& absolute) const {
    std::lock_guard<std::mutex> lock(m_builtinsMutex);
    return std::any_of(m_builtins.begin(), m_builtins.end(),
        [&absolute](const std::pair<const std::string, Builtin>& builtin) {
            return IsAtOrUnder(builtin.first, absolute);
        });
}

void MountableFileSystem::AddOwnBuiltinEntries(const std::string& absolute, std::vector<DirectoryEntry>& entries) const {
    std::lock_guard<std::mutex> lock(m_builtinsMutex);
    for (const auto& builtin : m_builtins) {
        if (VirtualParentOf(builtin.first) != absolute) {
            continue;
        }
        const std::string name = VirtualLastSegment(builtin.first);
        const bool alreadyListed = std::any_of(entries.begin(), entries.end(),
            [&name](const DirectoryEntry& entry) { return entry.name == name; });
        if (!alreadyListed) {
            entries.push_back(DirectoryEntry{name, DirectoryEntryType::File});
        }
    }
}

}
