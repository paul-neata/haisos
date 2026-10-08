#include "Filesystem.h"
#include "NoCriticalErrorDialogs.h"
#include <memory>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h> // utimes(), the WASM fallback for utimensat()
#include <algorithm>
#include <cstring>
#include "src/components/Logger/Logger.h"

namespace Haisos {

namespace {

// A block device, socket or FIFO is none of these; it is reported as a file,
// as it always has been.
char TypeOf(mode_t mode) {
    if (S_ISDIR(mode)) {
        return DirectoryEntryType::Dir;
    }
    return S_ISCHR(mode) ? DirectoryEntryType::CharDevice : DirectoryEntryType::File;
}

} // namespace

HostFileDescriptor::~HostFileDescriptor() {
    if (::close(m_hostFd) != 0) {
        LogWarning("HostFileDescriptor: closing host fd %d failed (errno %d)", m_hostFd, errno);
    }
}

ssize_t HostFileDescriptor::Read(void* buf, size_t count) {
    const ssize_t n = ::read(m_hostFd, buf, count);
    return n < 0 ? kIOError : n;
}

ssize_t HostFileDescriptor::Write(const void* buf, size_t count) {
    const ssize_t n = ::write(m_hostFd, buf, count);
    return n < 0 ? kIOError : n;
}

std::shared_ptr<IFileDescriptor> FileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    const int fd = ::open(pathname.c_str(), flags);
    return fd < 0 ? nullptr : HostFileDescriptor::Create(fd);
}

std::shared_ptr<IFileDescriptor> FileSystem::LocalOpenFile(const std::string& pathname, int flags, int mode) {
    const int fd = ::open(pathname.c_str(), flags, static_cast<mode_t>(mode));
    return fd < 0 ? nullptr : HostFileDescriptor::Create(fd);
}

int FileSystem::LocalCreateDirectory(const std::string& pathname, int mode) {
    return ::mkdir(pathname.c_str(), static_cast<mode_t>(mode));
}

int FileSystem::LocalRemoveDirectory(const std::string& pathname) {
    return ::rmdir(pathname.c_str());
}

int FileSystem::LocalRemoveFile(const std::string& pathname) {
    return ::unlink(pathname.c_str());
}

int FileSystem::LocalRename(const std::string& oldPath, const std::string& newPath) {
    if (::rename(oldPath.c_str(), newPath.c_str()) == 0) {
        return 0;
    }
    // Read errno immediately: nothing between the call and here may touch it.
    return errno == EXDEV ? kFileSystemCrossDevice : kFileSystemError;
}

int FileSystem::LocalSetTimes(const std::string& path,
                              const std::optional<FileDateTime>& accessTime,
                              const std::optional<FileDateTime>& modificationTime) {
#ifndef __EMSCRIPTEN__
    struct timespec times[2];
    times[0].tv_sec = accessTime ? accessTime->seconds : 0;
    times[0].tv_nsec = accessTime ? static_cast<long>(accessTime->nanoseconds) : UTIME_OMIT;
    times[1].tv_sec = modificationTime ? modificationTime->seconds : 0;
    times[1].tv_nsec = modificationTime ? static_cast<long>(modificationTime->nanoseconds) : UTIME_OMIT;
    return ::utimensat(AT_FDCWD, path.c_str(), times, 0) == 0 ? 0 : kFileSystemError;
#else
    // Emscripten has no utimensat; utimes stands in, with a time left alone
    // (nullopt) filled in from stat(), as UTIME_OMIT would leave it.
    struct stat st;
    const bool needCurrent = !accessTime || !modificationTime;
    if (needCurrent && ::stat(path.c_str(), &st) != 0) {
        return kFileSystemError;
    }
    struct timeval times[2];
    times[0].tv_sec = accessTime ? accessTime->seconds : st.st_atim.tv_sec;
    times[0].tv_usec = accessTime ? static_cast<long>(accessTime->nanoseconds / 1000)
                                  : static_cast<long>(st.st_atim.tv_nsec / 1000);
    times[1].tv_sec = modificationTime ? modificationTime->seconds : st.st_mtim.tv_sec;
    times[1].tv_usec = modificationTime ? static_cast<long>(modificationTime->nanoseconds / 1000)
                                        : static_cast<long>(st.st_mtim.tv_nsec / 1000);
    return ::utimes(path.c_str(), times) == 0 ? 0 : kFileSystemError;
#endif
}

std::vector<DirectoryEntry> FileSystem::LocalReadDirectory(const std::string& path) {
    std::vector<DirectoryEntry> entries;
    DIR* dir = ::opendir(path.c_str());
    if (!dir) {
        return entries;
    }

    struct dirent* entry = nullptr;
    while ((entry = ::readdir(dir)) != nullptr) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        DirectoryEntry de;
        de.name = entry->d_name;

        if (entry->d_type == DT_REG) {
            de.type = DirectoryEntryType::File;
        } else if (entry->d_type == DT_DIR) {
            de.type = DirectoryEntryType::Dir;
        } else if (entry->d_type == DT_CHR) {
            de.type = DirectoryEntryType::CharDevice;
        } else {
            struct stat st;
            std::string fullPath = path + "/" + entry->d_name;
            if (::stat(fullPath.c_str(), &st) == 0) {
                de.type = TypeOf(st.st_mode);
            } else {
                de.type = DirectoryEntryType::File;
            }
        }
        entries.push_back(std::move(de));
    }

    ::closedir(dir);
    return entries;
}

int FileSystem::LocalStat(const std::string& path, FileStatus& out) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        return -1;
    }
    out.type = TypeOf(st.st_mode);
    if (S_ISCHR(st.st_mode)) {
        out.deviceMajor = static_cast<uint32_t>(major(st.st_rdev));
        out.deviceMinor = static_cast<uint32_t>(minor(st.st_rdev));
    }
    out.size = static_cast<uint64_t>(st.st_size);
    out.blocks = static_cast<uint64_t>(st.st_blocks);
    out.linkCount = static_cast<uint64_t>(st.st_nlink);
    out.accessTime = FileDateTime{static_cast<int64_t>(st.st_atim.tv_sec), static_cast<uint32_t>(st.st_atim.tv_nsec)};
    out.modificationTime = FileDateTime{static_cast<int64_t>(st.st_mtim.tv_sec), static_cast<uint32_t>(st.st_mtim.tv_nsec)};
    out.changeTime = FileDateTime{static_cast<int64_t>(st.st_ctim.tv_sec), static_cast<uint32_t>(st.st_ctim.tv_nsec)};
    return 0;
}

// A POSIX host has no device names: /dev/null is a path like any other, and
// a physical filesystem reaches it only if it lies within its root.
bool FileSystem::IsDevicePath(const std::string& /*hostPath*/) {
    return false;
}

// No host here shows a dialog for a drive that is not ready.
NoCriticalErrorDialogs::NoCriticalErrorDialogs() = default;
NoCriticalErrorDialogs::~NoCriticalErrorDialogs() = default;

std::shared_ptr<IFileSystem> CreateFilesystem() {
    return FileSystem::Create();
}

} // namespace Haisos
