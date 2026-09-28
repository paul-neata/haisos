#include "PhysicalFileSystem.h"
#include "NoCriticalErrorDialogs.h"
#include "PhysicalPath.h"
#include "VirtualPath.h"
#include <filesystem>
#include <system_error>
#include "src/components/Logger/Logger.h"

#ifdef _WIN32
#include "windows/WindowsFullPhysicalFileSystem.h"
#else
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

// A UTF-8 string as a path, and back. std::filesystem reads a narrow string
// in the ANSI code page on Windows, where most names do not fit.
std::filesystem::path PathFromUtf8(const std::string& text) {
    return std::filesystem::u8path(text);
}

std::string Utf8FromPath(const std::filesystem::path& path) {
    return path.u8string();
}

} // namespace

std::shared_ptr<PhysicalFileSystem> PhysicalFileSystem::Create(const std::string& rootPath) {
#ifdef _WIN32
    // The root of the full physical filesystem holds the drives, and is no
    // directory of the host: that filesystem is a class of its own.
    if (IsFullFileSystemRoot(rootPath)) {
        return WindowsFullPhysicalFileSystem::Create();
    }
#endif
    return std::shared_ptr<PhysicalFileSystem>(new PhysicalFileSystem(rootPath));
}

PhysicalFileSystem::PhysicalFileSystem()
    : m_inner(FileSystem::Create())
{
}

PhysicalFileSystem::PhysicalFileSystem(const std::string& rootPath)
    : m_inner(FileSystem::Create())
{
    NoCriticalErrorDialogs noDialogs;
    // Only a relative root needs the current directory.
    std::error_code ec;
    const std::filesystem::path currentDirectory = std::filesystem::current_path(ec);
    std::string error;
    const auto hostPath = ResolvePhysicalPath(rootPath, ec ? std::string() : Utf8FromPath(currentDirectory), &error);
    if (!hostPath) {
        LogError("PhysicalFileSystem: invalid root path (%s): %s", error.c_str(), rootPath.c_str());
        return;
    }
    try {
        m_rootPath = Utf8FromPath(std::filesystem::weakly_canonical(std::filesystem::absolute(PathFromUtf8(*hostPath))));
    } catch (const std::exception& e) {
        LogError("PhysicalFileSystem: cannot canonicalize the root path (%s), taking it as it is: %s", e.what(), hostPath->c_str());
        m_rootPath = *hostPath;
    }
}

bool PhysicalFileSystem::HostPathOf(const std::vector<std::string>& segments, std::filesystem::path& hostPath) const {
    if (m_rootPath.empty()) {
        return false;
    }
    // Each segment is a plain host name (see ResolveOnHost), with no drive or
    // root of its own, so appending it can only go further down.
    std::filesystem::path joined = PathFromUtf8(m_rootPath);
    for (const auto& segment : segments) {
        joined /= PathFromUtf8(segment);
    }
    hostPath = joined;
    return true;
}

bool PhysicalFileSystem::IsWithin(const std::string& canonical) const {
    if (m_rootPath.empty()) {
        return false;
    }
    // Within the root means the root itself, or the root followed by a
    // separator and more. A root that already ends in a separator -- the top
    // of a disk, "/" or "C:\" -- brings its own, so any path starting with it
    // is within it.
    const char separator = static_cast<char>(std::filesystem::path::preferred_separator);
    const bool rootEndsInSeparator = m_rootPath.back() == '/' || m_rootPath.back() == separator;
    return canonical.size() >= m_rootPath.size() &&
        canonical.compare(0, m_rootPath.size(), m_rootPath) == 0 &&
        (rootEndsInSeparator || canonical.size() == m_rootPath.size() || canonical[m_rootPath.size()] == separator);
}

bool PhysicalFileSystem::CanonicalWithin(
    const std::filesystem::path& hostPath, const std::string& pathname, std::string& resolved) const
{
    // weakly_canonical resolves every symbolic link on the way that leads
    // somewhere, then appends what does not exist as it is.
    const std::string canonical = Utf8FromPath(std::filesystem::weakly_canonical(hostPath));
    if (!IsWithin(canonical)) {
        LogWarning("PhysicalFileSystem: path escapes root (%s): %s", m_rootPath.c_str(), pathname.c_str());
        return false;
    }

    // So a link still at the end is one that leads nowhere, and the check
    // above has not seen where it points. open() with O_CREAT would follow it
    // and create its target, wherever that is -- outside the root, possibly.
    if (FileSystem::IsLink(canonical)) {
        LogWarning("PhysicalFileSystem: path ends in a dangling symbolic link (%s): %s", m_rootPath.c_str(), pathname.c_str());
        return false;
    }

    resolved = canonical;
    return true;
}

bool PhysicalFileSystem::ResolveOnHost(const std::string& pathname, LastComponent last, std::string& resolved) const {
    // Checked, then used: something else on the host that swaps a directory
    // on the path for a link between this check and the operation could still
    // lead the operation outside the root. Only the last component is guarded
    // against that (O_NOFOLLOW, in LocalOpenFile); nothing inside Haisos can
    // create a link, so it takes an outside actor.
    NoCriticalErrorDialogs noDialogs;

    // Split exactly as the mounts and builtins over this filesystem split it,
    // so a path means the same to them as to the disk.
    std::vector<std::string> segments;
    if (!SplitVirtualPath(pathname, segments)) {
        LogWarning("PhysicalFileSystem: path escapes root (%s): %s", m_rootPath.c_str(), pathname.c_str());
        return false;
    }
    for (const auto& segment : segments) {
        std::string reason;
        if (!IsPlainHostName(segment, &reason)) {
            LogWarning("PhysicalFileSystem: refusing the name '%s' (%s): %s", segment.c_str(), reason.c_str(), pathname.c_str());
            return false;
        }
    }

    try {
        std::filesystem::path hostPath;
        if (!HostPathOf(segments, hostPath)) {
            LogDebug("PhysicalFileSystem: no directory on the host holds: %s", pathname.c_str());
            return false;
        }
        // A name this Windows takes for a device (NUL.txt on Windows 10) is
        // the device wherever the path leads: the console, a serial port.
        if (FileSystem::IsDevicePath(Utf8FromPath(hostPath))) {
            LogWarning("PhysicalFileSystem: refusing a path the host takes for a device (%s): %s", m_rootPath.c_str(), pathname.c_str());
            return false;
        }
        if (last == LastComponent::Follow) {
            return CanonicalWithin(hostPath, pathname, resolved);
        }

        // Only the directory holding the last component is resolved; the last
        // component is appended as it is. The top of the filesystem (and a
        // drive's root) has no such name, and is never removed or created --
        // nor a link: Stat asks, for every path.
        const std::filesystem::path name = hostPath.filename();
        if (segments.empty() || name.empty()) {
            LogDebug("PhysicalFileSystem: refusing to act on the root itself (%s): %s", m_rootPath.c_str(), pathname.c_str());
            return false;
        }
        std::string parent;
        if (!CanonicalWithin(hostPath.parent_path(), pathname, parent)) {
            return false;
        }
        resolved = Utf8FromPath(PathFromUtf8(parent) / name);
        return true;
    } catch (const std::exception& e) {
        LogDebug("PhysicalFileSystem: invalid path (%s): %s", e.what(), pathname.c_str());
        return false;
    }
}

bool PhysicalFileSystem::IsSymbolicLink(const std::string& pathname) const {
    std::string entry;
    if (!ResolveOnHost(pathname, LastComponent::Keep, entry)) {
        return false;
    }
    return FileSystem::IsLink(entry);
}

// Every open adds O_NOFOLLOW (on POSIX): ResolveOnHost has already refused a
// path ending in a link, so this only matters should one appear there after
// the check -- open() then fails rather than follow it.
int PhysicalFileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    std::string resolved;
    if (!ResolveOnHost(pathname, LastComponent::Follow, resolved)) {
        return -1;
    }
    return m_inner->LocalOpenFile(resolved, flags | kOpenNoFollow);
}

int PhysicalFileSystem::LocalOpenFile(const std::string& pathname, int flags, int mode) {
    std::string resolved;
    if (!ResolveOnHost(pathname, LastComponent::Follow, resolved)) {
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
    if (!ResolveOnHost(pathname, LastComponent::Keep, resolved)) {
        return -1;
    }
    return m_inner->LocalCreateDirectory(resolved, mode);
}

int PhysicalFileSystem::LocalRemoveDirectory(const std::string& pathname) {
    // As rmdir() does: never through a link at the path. rmdir() fails on
    // one on POSIX; on Windows it removes a directory link or a junction
    // itself -- never the directory it points at, either way.
    std::string resolved;
    if (!ResolveOnHost(pathname, LastComponent::Keep, resolved)) {
        return -1;
    }
    return m_inner->LocalRemoveDirectory(resolved);
}

int PhysicalFileSystem::LocalRemoveFile(const std::string& pathname) {
    // As unlink() does: a link at the path is removed itself, never the file
    // it points at -- which may lie anywhere.
    std::string resolved;
    if (!ResolveOnHost(pathname, LastComponent::Keep, resolved)) {
        return -1;
    }
    return m_inner->LocalRemoveFile(resolved);
}

std::vector<DirectoryEntry> PhysicalFileSystem::LocalReadDirectory(const std::string& path) {
    std::string resolved;
    if (!ResolveOnHost(path, LastComponent::Follow, resolved)) {
        return {};
    }
    return m_inner->LocalReadDirectory(resolved);
}

int PhysicalFileSystem::LocalStat(const std::string& path, FileStatus& out) {
    std::string resolved;
    if (!ResolveOnHost(path, LastComponent::Follow, resolved)) {
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
