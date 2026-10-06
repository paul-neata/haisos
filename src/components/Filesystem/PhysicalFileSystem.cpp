#include "PhysicalFileSystem.h"
#include "NoCriticalErrorDialogs.h"
#include "PhysicalPath.h"
#include "VirtualPath.h"
#include <filesystem>
#include <system_error>
#include "src/components/Logger/Logger.h"

#ifdef _WIN32
#include "windows/WindowsFullPhysicalFileSystem.h"
#endif

namespace Haisos {

namespace {

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

bool PhysicalFileSystem::ResolveOnHost(const std::string& pathname, Top top, std::string& resolved) const {
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
        // The top of the filesystem (and a drive's root) is never removed or
        // created.
        if (top == Top::Refused && (segments.empty() || hostPath.filename().empty())) {
            LogDebug("PhysicalFileSystem: refusing to act on the root itself (%s): %s", m_rootPath.c_str(), pathname.c_str());
            return false;
        }
        resolved = Utf8FromPath(hostPath);
        return true;
    } catch (const std::exception& e) {
        LogDebug("PhysicalFileSystem: invalid path (%s): %s", e.what(), pathname.c_str());
        return false;
    }
}

int PhysicalFileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    std::string resolved;
    if (!ResolveOnHost(pathname, Top::Allowed, resolved)) {
        return -1;
    }
    return m_inner->LocalOpenFile(resolved, flags);
}

int PhysicalFileSystem::LocalOpenFile(const std::string& pathname, int flags, int mode) {
    std::string resolved;
    if (!ResolveOnHost(pathname, Top::Allowed, resolved)) {
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
    // mkdir()
    std::string resolved;
    if (!ResolveOnHost(pathname, Top::Refused, resolved)) {
        return -1;
    }
    return m_inner->LocalCreateDirectory(resolved, mode);
}

int PhysicalFileSystem::LocalRemoveDirectory(const std::string& pathname) {
    // rmdir()
    std::string resolved;
    if (!ResolveOnHost(pathname, Top::Refused, resolved)) {
        return -1;
    }
    return m_inner->LocalRemoveDirectory(resolved);
}

int PhysicalFileSystem::LocalRemoveFile(const std::string& pathname) {
    // unlink()
    std::string resolved;
    if (!ResolveOnHost(pathname, Top::Refused, resolved)) {
        return -1;
    }
    return m_inner->LocalRemoveFile(resolved);
}

std::vector<DirectoryEntry> PhysicalFileSystem::LocalReadDirectory(const std::string& path) {
    std::string resolved;
    if (!ResolveOnHost(path, Top::Allowed, resolved)) {
        return {};
    }
    return m_inner->LocalReadDirectory(resolved);
}

int PhysicalFileSystem::LocalStat(const std::string& path, FileStatus& out) {
    std::string resolved;
    if (!ResolveOnHost(path, Top::Allowed, resolved)) {
        return -1;
    }
    return m_inner->LocalStat(resolved, out) == 0 ? 0 : -1;
}

std::string PhysicalFileSystem::AbsolutePathFor(const std::string& path) const {
    return NormalizeVirtualPath(path, "/");
}

} // namespace Haisos
