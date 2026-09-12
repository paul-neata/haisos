#include "PhysicalFileSystem.h"
#include <filesystem>
#include "src/components/Logger/Logger.h"

namespace Haisos {

PhysicalFileSystem::PhysicalFileSystem(const std::string& rootPath) {
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

int PhysicalFileSystem::OpenFile(const std::string& pathname, int flags) {
    std::string resolved;
    if (!ResolveWithinRoot(pathname, resolved)) {
        return -1;
    }
    return m_inner.OpenFile(resolved, flags);
}

int PhysicalFileSystem::OpenFile(const std::string& pathname, int flags, int mode) {
    std::string resolved;
    if (!ResolveWithinRoot(pathname, resolved)) {
        return -1;
    }
    return m_inner.OpenFile(resolved, flags, mode);
}

int PhysicalFileSystem::CloseFile(int fd) {
    return m_inner.CloseFile(fd);
}

ssize_t PhysicalFileSystem::ReadFile(int fd, void* buf, size_t count) {
    return m_inner.ReadFile(fd, buf, count);
}

ssize_t PhysicalFileSystem::WriteFile(int fd, const void* buf, size_t count) {
    return m_inner.WriteFile(fd, buf, count);
}

int PhysicalFileSystem::CreateDirectory(const std::string& pathname, int mode) {
    std::string resolved;
    if (!ResolveWithinRoot(pathname, resolved)) {
        return -1;
    }
    return m_inner.CreateDirectory(resolved, mode);
}

int PhysicalFileSystem::RemoveDirectory(const std::string& pathname) {
    std::string resolved;
    if (!ResolveWithinRoot(pathname, resolved)) {
        return -1;
    }
    return m_inner.RemoveDirectory(resolved);
}

int PhysicalFileSystem::ChangeDirectory(const std::string& path) {
    std::string resolved;
    if (!ResolveWithinRoot(path, resolved)) {
        return -1;
    }
    return m_inner.ChangeDirectory(resolved);
}

char* PhysicalFileSystem::GetCurrentDirectory(std::string& buf, size_t size) {
    return m_inner.GetCurrentDirectory(buf, size);
}

std::vector<DirectoryEntry> PhysicalFileSystem::ReadDirectory(const std::string& path) {
    std::string resolved;
    if (!ResolveWithinRoot(path, resolved)) {
        return {};
    }
    return m_inner.ReadDirectory(resolved);
}

} // namespace Haisos
