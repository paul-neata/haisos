#include "MountableFileSystem.h"
#include <algorithm>

namespace Haisos {

void MountableFileSystem::Mount(const std::string& whereToMount, std::shared_ptr<IFileSystem> toBeMounted) {
    m_mounts.Mount(AbsolutePathFor(whereToMount), std::move(toBeMounted));
}

void MountableFileSystem::Unmount(const std::string& mountedPath) {
    m_mounts.Unmount(AbsolutePathFor(mountedPath));
}

int MountableFileSystem::OpenFile(const std::string& pathname, int flags) {
    auto route = m_mounts.Resolve(AbsolutePathFor(pathname));
    if (route.filesystem) {
        return m_mounts.RegisterFd(route.filesystem, route.filesystem->OpenFile(route.innerPath, flags));
    }
    return LocalOpenFile(pathname, flags);
}

int MountableFileSystem::OpenFile(const std::string& pathname, int flags, int mode) {
    auto route = m_mounts.Resolve(AbsolutePathFor(pathname));
    if (route.filesystem) {
        return m_mounts.RegisterFd(route.filesystem, route.filesystem->OpenFile(route.innerPath, flags, mode));
    }
    return LocalOpenFile(pathname, flags, mode);
}

int MountableFileSystem::CloseFile(int fd) {
    IFileSystem* filesystem = nullptr;
    int innerFd = -1;
    if (MountPoints::IsSynthetic(fd) && m_mounts.LookupFd(fd, filesystem, innerFd)) {
        const int result = filesystem->CloseFile(innerFd);
        m_mounts.ReleaseFd(fd);
        return result;
    }
    return LocalCloseFile(fd);
}

ssize_t MountableFileSystem::ReadFile(int fd, void* buf, size_t count) {
    IFileSystem* filesystem = nullptr;
    int innerFd = -1;
    if (MountPoints::IsSynthetic(fd) && m_mounts.LookupFd(fd, filesystem, innerFd)) {
        return filesystem->ReadFile(innerFd, buf, count);
    }
    return LocalReadFile(fd, buf, count);
}

ssize_t MountableFileSystem::WriteFile(int fd, const void* buf, size_t count) {
    IFileSystem* filesystem = nullptr;
    int innerFd = -1;
    if (MountPoints::IsSynthetic(fd) && m_mounts.LookupFd(fd, filesystem, innerFd)) {
        return filesystem->WriteFile(innerFd, buf, count);
    }
    return LocalWriteFile(fd, buf, count);
}

int MountableFileSystem::CreateDirectory(const std::string& pathname, int mode) {
    auto route = m_mounts.Resolve(AbsolutePathFor(pathname));
    if (route.filesystem) {
        return route.filesystem->CreateDirectory(route.innerPath, mode);
    }
    return LocalCreateDirectory(pathname, mode);
}

int MountableFileSystem::RemoveDirectory(const std::string& pathname) {
    auto route = m_mounts.Resolve(AbsolutePathFor(pathname));
    if (route.filesystem) {
        return route.filesystem->RemoveDirectory(route.innerPath);
    }
    return LocalRemoveDirectory(pathname);
}

std::vector<DirectoryEntry> MountableFileSystem::ReadDirectory(const std::string& path) {
    const std::string absolute = AbsolutePathFor(path);
    auto route = m_mounts.Resolve(absolute);
    if (route.filesystem) {
        return route.filesystem->ReadDirectory(route.innerPath);
    }

    std::vector<DirectoryEntry> entries = LocalReadDirectory(path);
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
    return entries;
}

}
