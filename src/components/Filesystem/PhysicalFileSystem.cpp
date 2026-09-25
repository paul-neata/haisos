#include "PhysicalFileSystem.h"
#include "VirtualPath.h"
#include <filesystem>
#include "src/components/Logger/Logger.h"

namespace Haisos {

std::shared_ptr<PhysicalFileSystem> PhysicalFileSystem::Create(const std::string& rootPath) {
    return std::shared_ptr<PhysicalFileSystem>(new PhysicalFileSystem(rootPath));
}

PhysicalFileSystem::PhysicalFileSystem(const std::string& rootPath)
    : m_inner(FileSystem::Create())
{
    try {
        m_rootPath = std::filesystem::weakly_canonical(std::filesystem::absolute(rootPath)).string();
    } catch (const std::exception& e) {
        LogError("PhysicalFileSystem: invalid root path (%s): %s", e.what(), rootPath.c_str());
        m_rootPath = rootPath;
    }
}

bool PhysicalFileSystem::ResolveWithinRoot(const std::string& pathname, std::string& resolved) const {
    try {
        std::filesystem::path requested(pathname);
        // Treat absolute paths as absolute *within the jail*: strip the root
        // indicator so "/foo" and "foo" both resolve under m_rootPath.
        std::filesystem::path joined = requested.is_absolute()
            ? std::filesystem::path(m_rootPath) / requested.relative_path()
            : std::filesystem::path(m_rootPath) / requested;

        std::filesystem::path canonical = std::filesystem::weakly_canonical(joined);
        const std::string canonicalStr = canonical.string();

        if (canonicalStr.size() < m_rootPath.size() ||
            canonicalStr.compare(0, m_rootPath.size(), m_rootPath) != 0 ||
            (canonicalStr.size() > m_rootPath.size() &&
             canonicalStr[m_rootPath.size()] != std::filesystem::path::preferred_separator)) {
            LogWarning("PhysicalFileSystem: path escapes root (%s): %s", m_rootPath.c_str(), pathname.c_str());
            return false;
        }

        resolved = canonicalStr;
        return true;
    } catch (const std::exception& e) {
        LogDebug("PhysicalFileSystem: invalid path (%s): %s", e.what(), pathname.c_str());
        return false;
    }
}

int PhysicalFileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    std::string resolved;
    if (!ResolveWithinRoot(pathname, resolved)) {
        return -1;
    }
    return m_inner->LocalOpenFile(resolved, flags);
}

int PhysicalFileSystem::LocalOpenFile(const std::string& pathname, int flags, int mode) {
    std::string resolved;
    if (!ResolveWithinRoot(pathname, resolved)) {
        return -1;
    }
    return m_inner->LocalOpenFile(resolved, flags, mode);
}

int PhysicalFileSystem::LocalCloseFile(int fd) {
    return m_inner->LocalCloseFile(fd);
}

ssize_t PhysicalFileSystem::LocalReadFile(int fd, void* buf, size_t count) {
    return m_inner->LocalReadFile(fd, buf, count);
}

ssize_t PhysicalFileSystem::LocalWriteFile(int fd, const void* buf, size_t count) {
    return m_inner->LocalWriteFile(fd, buf, count);
}

int PhysicalFileSystem::LocalCreateDirectory(const std::string& pathname, int mode) {
    std::string resolved;
    if (!ResolveWithinRoot(pathname, resolved)) {
        return -1;
    }
    return m_inner->LocalCreateDirectory(resolved, mode);
}

int PhysicalFileSystem::LocalRemoveDirectory(const std::string& pathname) {
    std::string resolved;
    if (!ResolveWithinRoot(pathname, resolved)) {
        return -1;
    }
    return m_inner->LocalRemoveDirectory(resolved);
}

int PhysicalFileSystem::LocalRemoveFile(const std::string& pathname) {
    std::string resolved;
    if (!ResolveWithinRoot(pathname, resolved)) {
        return -1;
    }
    return m_inner->LocalRemoveFile(resolved);
}

std::vector<DirectoryEntry> PhysicalFileSystem::LocalReadDirectory(const std::string& path) {
    std::string resolved;
    if (!ResolveWithinRoot(path, resolved)) {
        return {};
    }
    return m_inner->LocalReadDirectory(resolved);
}

int PhysicalFileSystem::LocalStat(const std::string& path, FileStatus& out) {
    std::string resolved;
    if (!ResolveWithinRoot(path, resolved)) {
        return -1;
    }
    return m_inner->LocalStat(resolved, out);
}

std::string PhysicalFileSystem::AbsolutePathFor(const std::string& path) const {
    return NormalizeVirtualPath(path, "/");
}

} // namespace Haisos
