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
// the time lies before that, where no FILETIME is, or after the last one whose
// ticks still fit an int64_t.
bool ToFileTime(const FileDateTime& time, FILETIME& out) {
    constexpr int64_t kSecondsTo1601 = 11644473600;
    // The largest whole second whose ticks since 1601, with the nanoseconds
    // added below, stay inside an int64_t -- checked before multiplying, so
    // no signed overflow is ever evaluated.
    constexpr int64_t kMaxSeconds = (INT64_MAX - 9999999) / 10000000 - kSecondsTo1601;
    if (time.seconds < -kSecondsTo1601 || time.seconds > kMaxSeconds) {
        return false;
    }
    const int64_t ticks = (time.seconds + kSecondsTo1601) * 10000000 + time.nanoseconds / 100;
    out.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFF);
    out.dwHighDateTime = static_cast<DWORD>((static_cast<uint64_t>(ticks) >> 32) & 0xFFFFFFFF);
    return true;
}

// A path as GetFullPathNameW spells it: \ separators, a trailing one dropped
// (a drive root keeps its own, "C:\"). False when Windows cannot say.
bool FullWindowsPath(const std::wstring& path, std::wstring& out) {
    const DWORD length = ::GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (length == 0) {
        return false;
    }
    std::wstring full(length, L'\0');
    const DWORD written = ::GetFullPathNameW(path.c_str(), length, full.data(), nullptr);
    if (written == 0 || written >= length) {
        return false;
    }
    full.resize(written);
    std::replace(full.begin(), full.end(), L'/', L'\\');
    if (full.size() > 3 && full.back() == L'\\') {
        full.pop_back();
    }
    out = std::move(full);
    return true;
}

// Whether path names base itself or something below it, both as FullWindowsPath
// spells them, without regard to case.
bool IsSameOrBelow(const std::wstring& path, const std::wstring& base) {
    const int baseLength = static_cast<int>(base.size());
    if (::CompareStringOrdinal(path.c_str(), static_cast<int>(path.size()),
                               base.c_str(), baseLength, TRUE) == CSTR_EQUAL) {
        return true;
    }
    return path.size() > base.size() && path[base.size()] == L'\\'
        && ::CompareStringOrdinal(path.c_str(), baseLength, base.c_str(), baseLength, TRUE) == CSTR_EQUAL;
}

// Puts back an empty directory RemoveDirectoryW took away to make room for a
// move that then did not happen: its times and attributes back. Best effort,
// as rename() cannot promise more once the move has failed -- a failure is
// logged and changes nothing else.
void RestoreRemovedDirectory(const std::wstring& wide, const WIN32_FILE_ATTRIBUTE_DATA& data) {
    if (!::CreateDirectoryW(wide.c_str(), nullptr)) {
        LogWarning("FileSystem: recreating the directory \"%s\" a failed rename removed did not work (error %lu)",
                   WideToUtf8(wide).c_str(), ::GetLastError());
        return;
    }
    const HANDLE handle = ::CreateFileW(wide.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        LogWarning("FileSystem: opening the recreated directory \"%s\" to put its times back did not work (error %lu)",
                   WideToUtf8(wide).c_str(), ::GetLastError());
    } else {
        if (!::SetFileTime(handle, &data.ftCreationTime, &data.ftLastAccessTime, &data.ftLastWriteTime)) {
            LogWarning("FileSystem: putting back the times of the recreated directory \"%s\" did not work (error %lu)",
                       WideToUtf8(wide).c_str(), ::GetLastError());
        }
        ::CloseHandle(handle);
    }
    if (data.dwFileAttributes != FILE_ATTRIBUTE_DIRECTORY) {
        if (!::SetFileAttributesW(wide.c_str(), data.dwFileAttributes)) {
            LogWarning("FileSystem: putting back the attributes of the recreated directory \"%s\" did not work (error %lu)",
                       WideToUtf8(wide).c_str(), ::GetLastError());
        }
    }
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
    if (oldAttrs & FILE_ATTRIBUTE_DIRECTORY) {
        // rename() refuses a directory moved to itself or below itself
        // (EINVAL), changing nothing; MoveFileExW does not, so the refusal is
        // made here, before anything is touched. The identical and case-only
        // spellings returned above, so "itself" here catches only another
        // spelling of the same path, such as C:\x\d against C:/x/d/.
        std::wstring oldFull;
        std::wstring newFull;
        if (!FullWindowsPath(oldWide, oldFull) || !FullWindowsPath(newWide, newFull)
            || IsSameOrBelow(newFull, oldFull)) {
            return kFileSystemError;
        }
    }
    const DWORD newAttrs = ::GetFileAttributesW(newWide.c_str());
    // An empty target directory is taken away to make room: MoveFileExW
    // cannot replace a directory. rename() changes nothing when it fails, so
    // what was taken away is put back -- times and attributes included -- if
    // the move then does not happen.
    bool removedTargetDirectory = false;
    WIN32_FILE_ATTRIBUTE_DATA targetData;
    if (newAttrs != INVALID_FILE_ATTRIBUTES) {
        const bool oldIsDir = (oldAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool newIsDir = (newAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (oldIsDir != newIsDir) {
            return kFileSystemError; // a file would replace a directory, or a directory a file
        }
        if (newIsDir) {
            // RemoveDirectoryW refuses a directory that is not empty, and so
            // does the rename -- exactly the replacement rename() allows.
            // Without the target's own times and attributes there would be no
            // putting it back, so they are read first.
            if (!::GetFileAttributesExW(newWide.c_str(), GetFileExInfoStandard, &targetData)) {
                return kFileSystemError;
            }
            if (!::RemoveDirectoryW(newWide.c_str())) {
                return kFileSystemError;
            }
            removedTargetDirectory = true;
        }
    }
    // Never MOVEFILE_COPY_ALLOWED: copying across volumes is the caller's
    // decision (mv's copy-then-remove), not the filesystem's.
    if (!::MoveFileExW(oldWide.c_str(), newWide.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const DWORD moveError = ::GetLastError(); // before any restore call
        if (removedTargetDirectory) {
            RestoreRemovedDirectory(newWide, targetData);
        }
        return moveError == ERROR_NOT_SAME_DEVICE ? kFileSystemCrossDevice : kFileSystemError;
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
