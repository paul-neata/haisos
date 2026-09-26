#include "PhysicalFileSystem.h"
#include "VirtualPath.h"
#include <filesystem>
#include <system_error>
#include "src/components/Logger/Logger.h"

#ifndef _WIN32
#include <fcntl.h>
#endif

namespace Haisos {

namespace {

// Added to every open: see LocalOpenFile.
#ifdef _WIN32
constexpr int kOpenNoFollow = 0;
#else
constexpr int kOpenNoFollow = O_NOFOLLOW;
#endif

} // namespace

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

std::filesystem::path PhysicalFileSystem::JoinUnderRoot(const std::string& pathname) const {
    std::filesystem::path requested(pathname);
    // Every path is taken as relative to the jail, whatever root it names:
    // "/foo" and "foo" alike resolve under m_rootPath. relative_path() drops
    // that root for every path, not only an absolute one -- which matters on
    // Windows, where "/foo" has a root directory but no drive, so is not
    // absolute, and joined as it is would name the drive's own root (C:\foo)
    // rather than a path in the jail. A drive or share a path might name
    // ("C:\foo", "\\server\share\foo") is dropped the same way.
    std::filesystem::path joined = std::filesystem::path(m_rootPath) / requested.relative_path();
    // "." and ".." are resolved here, before the disk is asked anything, so
    // that none can hide a component from weakly_canonical's walk: given
    // "missing/../link", it stops at "missing", which does not exist, and
    // hands back "link" merely normalized -- unresolved, whatever it links to.
    return joined.lexically_normal();
}

bool PhysicalFileSystem::CanonicalWithinRoot(
    const std::filesystem::path& joined, const std::string& pathname, std::string& resolved) const
{
    // weakly_canonical resolves every symbolic link on the way that leads
    // somewhere, then appends what does not exist as it is.
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(joined);
    const std::string canonicalStr = canonical.string();

    // Within the root means the root itself, or the root followed by a
    // separator and more. A root that already ends in a separator -- the
    // top of a disk, "/" or "C:\" -- brings its own, so any path starting
    // with it is within it.
    const bool rootEndsInSeparator = !m_rootPath.empty() &&
        (m_rootPath.back() == '/' || m_rootPath.back() == std::filesystem::path::preferred_separator);
    if (canonicalStr.size() < m_rootPath.size() ||
        canonicalStr.compare(0, m_rootPath.size(), m_rootPath) != 0 ||
        (!rootEndsInSeparator && canonicalStr.size() > m_rootPath.size() &&
         canonicalStr[m_rootPath.size()] != std::filesystem::path::preferred_separator)) {
        LogWarning("PhysicalFileSystem: path escapes root (%s): %s", m_rootPath.c_str(), pathname.c_str());
        return false;
    }

    // So a link still at the end is one that leads nowhere, and the check
    // above has not seen where it points. open() with O_CREAT would follow it
    // and create its target, wherever that is -- outside the root, possibly.
    std::error_code ec;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(canonical, ec))) {
        LogWarning("PhysicalFileSystem: path ends in a dangling symbolic link (%s): %s", m_rootPath.c_str(), pathname.c_str());
        return false;
    }

    resolved = canonicalStr;
    return true;
}

bool PhysicalFileSystem::ResolveWithinRoot(const std::string& pathname, LastComponent last, std::string& resolved) const {
    // Checked, then used: something else on the host that swaps a directory
    // on the path for a link between this check and the operation could still
    // lead the operation outside the root. Only the last component is guarded
    // against that (O_NOFOLLOW, in LocalOpenFile); nothing inside Haisos can
    // create a link, so it takes an outside actor.
    try {
        const std::filesystem::path joined = JoinUnderRoot(pathname);
        if (last == LastComponent::Follow) {
            return CanonicalWithinRoot(joined, pathname, resolved);
        }

        // Only the directory holding the last component is resolved; the last
        // component is appended as it is. The root itself has no such name
        // here (nor has anything above it), and is never removed or created.
        const std::filesystem::path name = joined.filename();
        if (name.empty() || name == "." || name == "..") {
            LogWarning("PhysicalFileSystem: refusing to act on the root itself (%s): %s", m_rootPath.c_str(), pathname.c_str());
            return false;
        }
        std::string parent;
        if (!CanonicalWithinRoot(joined.parent_path(), pathname, parent)) {
            return false;
        }
        resolved = (std::filesystem::path(parent) / name).string();
        return true;
    } catch (const std::exception& e) {
        LogDebug("PhysicalFileSystem: invalid path (%s): %s", e.what(), pathname.c_str());
        return false;
    }
}

bool PhysicalFileSystem::IsSymbolicLink(const std::string& pathname) const {
    std::string entry;
    if (!ResolveWithinRoot(pathname, LastComponent::Keep, entry)) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_symlink(std::filesystem::symlink_status(entry, ec));
}

// Every open adds O_NOFOLLOW (on POSIX): ResolveWithinRoot has already refused
// a path ending in a link, so this only matters should one appear there after
// the check -- open() then fails rather than follow it.
int PhysicalFileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    std::string resolved;
    if (!ResolveWithinRoot(pathname, LastComponent::Follow, resolved)) {
        return -1;
    }
    return m_inner->LocalOpenFile(resolved, flags | kOpenNoFollow);
}

int PhysicalFileSystem::LocalOpenFile(const std::string& pathname, int flags, int mode) {
    std::string resolved;
    if (!ResolveWithinRoot(pathname, LastComponent::Follow, resolved)) {
        return -1;
    }
    return m_inner->LocalOpenFile(resolved, flags | kOpenNoFollow, mode);
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
    // As mkdir() does: a link already at the path, even a dangling one, means
    // the path exists, and nothing is created through it.
    std::string resolved;
    if (!ResolveWithinRoot(pathname, LastComponent::Keep, resolved)) {
        return -1;
    }
    return m_inner->LocalCreateDirectory(resolved, mode);
}

int PhysicalFileSystem::LocalRemoveDirectory(const std::string& pathname) {
    // As rmdir() does: never through a link at the path. rmdir() fails on
    // one on POSIX; on Windows it removes a directory link itself -- never the
    // directory it points at, either way.
    std::string resolved;
    if (!ResolveWithinRoot(pathname, LastComponent::Keep, resolved)) {
        return -1;
    }
    return m_inner->LocalRemoveDirectory(resolved);
}

int PhysicalFileSystem::LocalRemoveFile(const std::string& pathname) {
    // As unlink() does: a link at the path is removed itself, never the file
    // it points at -- which may lie anywhere.
    std::string resolved;
    if (!ResolveWithinRoot(pathname, LastComponent::Keep, resolved)) {
        return -1;
    }
    return m_inner->LocalRemoveFile(resolved);
}

std::vector<DirectoryEntry> PhysicalFileSystem::LocalReadDirectory(const std::string& path) {
    std::string resolved;
    if (!ResolveWithinRoot(path, LastComponent::Follow, resolved)) {
        return {};
    }
    return m_inner->LocalReadDirectory(resolved);
}

int PhysicalFileSystem::LocalStat(const std::string& path, FileStatus& out) {
    std::string resolved;
    if (!ResolveWithinRoot(path, LastComponent::Follow, resolved)) {
        return -1;
    }
    if (m_inner->LocalStat(resolved, out) != 0) {
        return -1;
    }
    // Everything else describes what the path leads to, as stat() does; this
    // is what lstat() would add. Set either way, so no earlier value lingers.
    out.symbolicLink = IsSymbolicLink(path);
    return 0;
}

std::string PhysicalFileSystem::AbsolutePathFor(const std::string& path) const {
    return NormalizeVirtualPath(path, "/");
}

} // namespace Haisos
