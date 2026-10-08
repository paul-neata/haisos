#include "Filesystem.h"
#include "NoCriticalErrorDialogs.h"
#include <memory>
#include <cerrno>
#include <io.h>
#include <direct.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <windows.h>
#undef CreateDirectory
#undef RemoveDirectory
#undef GetCurrentDirectory
#include <cstring>
#include <climits>
#include <string>
#include <algorithm>
#include "src/components/Logger/Logger.h"
#include "src/components/libheaders/CrtInvalidParameterAsError.h"
#include "src/components/libheaders/WideText.h"

namespace Haisos {

// Every C runtime call below runs with a CrtInvalidParameterAsError in scope.
// The CRT meets a descriptor that is not open -- or a count it will not take --
// by ending the whole program through its invalid parameter handler, where
// close() and read() on POSIX just fail. In scope, the call fails with -1 as
// the IFileDescriptor contract says, and only the caller hears of it.
//
// Paths arrive as UTF-8 and reach Windows as UTF-16, through the "W" calls:
// the narrow ones read a path in the ANSI code page, where a name such as
// "café" in UTF-8 names another file entirely. A path that is not valid UTF-8
// names no file, and fails with EINVAL.

namespace {

bool ToWidePath(const std::string& pathname, std::wstring& wide) {
    if (!Utf8ToWide(pathname, wide)) {
        errno = EINVAL;
        return false;
    }
    return true;
}

// A FileDateTime as a FILETIME: 100 ns ticks since 1601-01-01 UTC. False if
// the time lies before that, where no FILETIME is.
bool ToFileTime(const FileDateTime& time, FILETIME& out) {
    constexpr int64_t kSecondsTo1601 = 11644473600;
    if (time.seconds < -kSecondsTo1601) {
        return false;
    }
    const int64_t ticks = (time.seconds + kSecondsTo1601) * 10000000 + time.nanoseconds / 100;
    out.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFF);
    out.dwHighDateTime = static_cast<DWORD>((static_cast<uint64_t>(ticks) >> 32) & 0xFFFFFFFF);
    return true;
}

} // namespace

HostFileDescriptor::~HostFileDescriptor() {
    CrtInvalidParameterAsError crtErrors;
    if (::_close(m_hostFd) != 0) {
        LogWarning("HostFileDescriptor: closing host fd %d failed (errno %d)", m_hostFd, errno);
    }
}

ssize_t HostFileDescriptor::Read(void* buf, size_t count) {
    NoCriticalErrorDialogs noDialogs;
    CrtInvalidParameterAsError crtErrors;
    // A short read is allowed; a silently truncated count is not.
    const size_t capped = (std::min)(count, static_cast<size_t>(INT_MAX));
    const int n = ::_read(m_hostFd, buf, static_cast<unsigned int>(capped));
    return n < 0 ? kIOError : n;
}

ssize_t HostFileDescriptor::Write(const void* buf, size_t count) {
    NoCriticalErrorDialogs noDialogs;
    CrtInvalidParameterAsError crtErrors;
    const size_t capped = (std::min)(count, static_cast<size_t>(INT_MAX));
    const int n = ::_write(m_hostFd, buf, static_cast<unsigned int>(capped));
    return n < 0 ? kIOError : n;
}

NoCriticalErrorDialogs::NoCriticalErrorDialogs()
    : m_previousMode(::GetThreadErrorMode())
{
    ::SetThreadErrorMode(m_previousMode | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, nullptr);
}

NoCriticalErrorDialogs::~NoCriticalErrorDialogs() {
    ::SetThreadErrorMode(m_previousMode, nullptr);
}

std::shared_ptr<IFileDescriptor> FileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    NoCriticalErrorDialogs noDialogs;
    CrtInvalidParameterAsError crtErrors;
    std::wstring wide;
    if (!ToWidePath(pathname, wide)) {
        return nullptr;
    }
    const int fd = ::_wopen(wide.c_str(), flags);
    return fd < 0 ? nullptr : HostFileDescriptor::Create(fd);
}

std::shared_ptr<IFileDescriptor> FileSystem::LocalOpenFile(const std::string& pathname, int flags, int mode) {
    NoCriticalErrorDialogs noDialogs;
    CrtInvalidParameterAsError crtErrors;
    std::wstring wide;
    if (!ToWidePath(pathname, wide)) {
        return nullptr;
    }
    const int fd = ::_wopen(wide.c_str(), flags, mode);
    return fd < 0 ? nullptr : HostFileDescriptor::Create(fd);
}

int FileSystem::LocalCreateDirectory(const std::string& pathname, int /*mode*/) {
    NoCriticalErrorDialogs noDialogs;
    CrtInvalidParameterAsError crtErrors;
    std::wstring wide;
    return ToWidePath(pathname, wide) ? ::_wmkdir(wide.c_str()) : -1;
}

int FileSystem::LocalRemoveDirectory(const std::string& pathname) {
    NoCriticalErrorDialogs noDialogs;
    CrtInvalidParameterAsError crtErrors;
    std::wstring wide;
    return ToWidePath(pathname, wide) ? ::_wrmdir(wide.c_str()) : -1;
}

int FileSystem::LocalRemoveFile(const std::string& pathname) {
    NoCriticalErrorDialogs noDialogs;
    CrtInvalidParameterAsError crtErrors;
    std::wstring wide;
    return ToWidePath(pathname, wide) ? ::_wunlink(wide.c_str()) : -1;
}

int FileSystem::LocalRename(const std::string& oldPath, const std::string& newPath) {
    NoCriticalErrorDialogs noDialogs;
    std::wstring oldWide;
    std::wstring newWide;
    if (!ToWidePath(oldPath, oldWide) || !ToWidePath(newPath, newWide)) {
        return kFileSystemError;
    }

    // Renaming a path to itself does nothing and succeeds -- when it is there.
    // Only an *identical* path returns early: a case-only rename ("a" -> "A")
    // is a real rename on Windows, and goes straight to MoveFileExW below,
    // without the replace steps (replacing a file with itself, or taking a
    // directory away and putting it back, would only be in the way).
    if (oldWide == newWide) {
        return ::GetFileAttributesW(oldWide.c_str()) == INVALID_FILE_ATTRIBUTES
            ? kFileSystemError : 0;
    }
    if (::CompareStringOrdinal(oldWide.c_str(), -1, newWide.c_str(), -1, TRUE) == CSTR_EQUAL) {
        return ::MoveFileExW(oldWide.c_str(), newWide.c_str(), 0) ? 0 : kFileSystemError;
    }

    // POSIX rename() semantics on top of MoveFileExW: a file at the new path is
    // replaced, a directory only by an empty directory.
    const DWORD oldAttrs = ::GetFileAttributesW(oldWide.c_str());
    if (oldAttrs == INVALID_FILE_ATTRIBUTES) {
        return kFileSystemError; // rename() reports ENOENT before anything else
    }
    const DWORD newAttrs = ::GetFileAttributesW(newWide.c_str());
    if (newAttrs != INVALID_FILE_ATTRIBUTES) {
        const bool oldIsDir = (oldAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool newIsDir = (newAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (oldIsDir != newIsDir) {
            return kFileSystemError; // a file would replace a directory, or a directory a file
        }
        if (newIsDir) {
            // RemoveDirectoryW refuses a directory that is not empty, and so
            // does the rename -- exactly the replacement rename() allows.
            if (!::RemoveDirectoryW(newWide.c_str())) {
                return kFileSystemError;
            }
        }
    }
    // Never MOVEFILE_COPY_ALLOWED: copying across volumes is the caller's
    // decision (mv's copy-then-remove), not the filesystem's.
    if (!::MoveFileExW(oldWide.c_str(), newWide.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        return ::GetLastError() == ERROR_NOT_SAME_DEVICE ? kFileSystemCrossDevice : kFileSystemError;
    }
    return 0;
}

int FileSystem::LocalSetTimes(const std::string& path,
                              const std::optional<FileDateTime>& accessTime,
                              const std::optional<FileDateTime>& modificationTime) {
    NoCriticalErrorDialogs noDialogs;
    std::wstring wide;
    if (!ToWidePath(path, wide)) {
        return kFileSystemError;
    }
    // FILE_FLAG_BACKUP_SEMANTICS: a directory opens too, not only a file.
    const HANDLE handle = ::CreateFileW(wide.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return kFileSystemError;
    }
    FILETIME accessFt;
    FILETIME modificationFt;
    FILETIME* accessPtr = nullptr;
    FILETIME* modificationPtr = nullptr;
    if (accessTime && !ToFileTime(*accessTime, accessFt)) {
        ::CloseHandle(handle);
        return kFileSystemError; // before 1601-01-01, before FILETIME begins
    }
    if (modificationTime && !ToFileTime(*modificationTime, modificationFt)) {
        ::CloseHandle(handle);
        return kFileSystemError;
    }
    // A null pointer leaves that time alone, as utimensat's UTIME_OMIT does.
    if (accessTime) {
        accessPtr = &accessFt;
    }
    if (modificationTime) {
        modificationPtr = &modificationFt;
    }
    const BOOL set = ::SetFileTime(handle, nullptr, accessPtr, modificationPtr);
    ::CloseHandle(handle);
    return set ? 0 : kFileSystemError;
}

int FileSystem::LocalStat(const std::string& path, FileStatus& out) {
    NoCriticalErrorDialogs noDialogs;
    CrtInvalidParameterAsError crtErrors;
    std::wstring wide;
    struct _stat64 st;
    if (!ToWidePath(path, wide) || ::_wstat64(wide.c_str(), &st) != 0) {
        return -1;
    }
    out.type = (st.st_mode & _S_IFDIR) ? DirectoryEntryType::Dir : DirectoryEntryType::File;
    out.size = static_cast<uint64_t>(st.st_size);
    // _stat64 has no block count; a 4 KB cluster is the NTFS default.
    out.blocks = ((out.size + 4095) / 4096) * 8;
    out.linkCount = static_cast<uint64_t>(st.st_nlink);
    // Whole seconds only, and st_ctime is the creation time on Windows.
    out.accessTime = FileDateTime{static_cast<int64_t>(st.st_atime), 0};
    out.modificationTime = FileDateTime{static_cast<int64_t>(st.st_mtime), 0};
    out.changeTime = FileDateTime{static_cast<int64_t>(st.st_ctime), 0};
    return 0;
}

std::vector<DirectoryEntry> FileSystem::LocalReadDirectory(const std::string& path) {
    NoCriticalErrorDialogs noDialogs;
    std::vector<DirectoryEntry> entries;

    std::wstring searchPath;
    if (!ToWidePath(path, searchPath)) {
        return entries;
    }
    // A drive's root ("C:\") ends in a separator already.
    if (searchPath.empty() || (searchPath.back() != L'\\' && searchPath.back() != L'/')) {
        searchPath += L'\\';
    }
    searchPath += L'*';

    WIN32_FIND_DATAW fd;
    HANDLE hFind = ::FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return entries;
    }

    do {
        if (std::wcscmp(fd.cFileName, L".") == 0 || std::wcscmp(fd.cFileName, L"..") == 0) {
            continue;
        }

        DirectoryEntry de;
        de.name = WideToUtf8(fd.cFileName);
        de.type = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DirectoryEntryType::Dir : DirectoryEntryType::File;
        entries.push_back(std::move(de));
    } while (::FindNextFileW(hFind, &fd));

    ::FindClose(hFind);
    return entries;
}

bool FileSystem::IsDevicePath(const std::string& hostPath) {
    std::wstring wide;
    if (!ToWidePath(hostPath, wide) || wide.empty()) {
        return false;
    }
    // A device comes back in the device namespace, as \\.\NUL; a file's full
    // path is the path itself. The buffer holds any full path of a file a
    // Windows call without the \\?\ prefix may name.
    wchar_t full[MAX_PATH + 1];
    const DWORD length = ::GetFullPathNameW(wide.c_str(), static_cast<DWORD>(MAX_PATH + 1), full, nullptr);
    if (length == 0 || length > MAX_PATH) {
        return false;
    }
    return length >= 4 && full[0] == L'\\' && full[1] == L'\\' && full[2] == L'.' && full[3] == L'\\';
}

std::shared_ptr<IFileSystem> CreateFilesystem() {
    return FileSystem::Create();
}

} // namespace Haisos
