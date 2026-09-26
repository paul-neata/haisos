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
#include <string>
#include <algorithm>
#include "src/components/libheaders/CrtInvalidParameterAsError.h"
#include "src/components/libheaders/WideText.h"

// The tag of a symbolic link WSL makes on a Windows drive (in winnt.h only
// since the Windows 10 SDKs).
#ifndef IO_REPARSE_TAG_LX_SYMLINK
#define IO_REPARSE_TAG_LX_SYMLINK 0xA000001DL
#endif

namespace Haisos {

// Every C runtime call below runs with a CrtInvalidParameterAsError in scope.
// A descriptor is an int any caller may hand in, and the CRT meets one that
// is not open -- or a count it will not take -- by ending the whole program
// through its invalid parameter handler, where close() and read() on POSIX
// just fail. In scope, the call fails with -1 as the IFileSystem contract
// says, and only the caller hears of it.
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

} // namespace

NoCriticalErrorDialogs::NoCriticalErrorDialogs()
    : m_previousMode(::GetThreadErrorMode())
{
    ::SetThreadErrorMode(m_previousMode | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, nullptr);
}

NoCriticalErrorDialogs::~NoCriticalErrorDialogs() {
    ::SetThreadErrorMode(m_previousMode, nullptr);
}

int FileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    NoCriticalErrorDialogs noDialogs;
    CrtInvalidParameterAsError crtErrors;
    std::wstring wide;
    return ToWidePath(pathname, wide) ? ::_wopen(wide.c_str(), flags) : -1;
}

int FileSystem::LocalOpenFile(const std::string& pathname, int flags, int mode) {
    NoCriticalErrorDialogs noDialogs;
    CrtInvalidParameterAsError crtErrors;
    std::wstring wide;
    return ToWidePath(pathname, wide) ? ::_wopen(wide.c_str(), flags, mode) : -1;
}

int FileSystem::LocalCloseFile(int fd) {
    CrtInvalidParameterAsError crtErrors;
    return ::_close(fd);
}

ssize_t FileSystem::LocalReadFile(int fd, void* buf, size_t count) {
    NoCriticalErrorDialogs noDialogs;
    CrtInvalidParameterAsError crtErrors;
    return ::_read(fd, buf, static_cast<unsigned int>(count));
}

ssize_t FileSystem::LocalWriteFile(int fd, const void* buf, size_t count) {
    NoCriticalErrorDialogs noDialogs;
    CrtInvalidParameterAsError crtErrors;
    return ::_write(fd, buf, static_cast<unsigned int>(count));
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

bool FileSystem::IsLink(const std::string& hostPath) {
    NoCriticalErrorDialogs noDialogs;
    std::wstring wide;
    if (!ToWidePath(hostPath, wide)) {
        return false;
    }
    const DWORD attributes = ::GetFileAttributesW(wide.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        return false;
    }
    // Many a reparse point is no link -- a OneDrive placeholder, a
    // deduplicated file -- so its tag decides. FindFirstFileW reports it,
    // and matches the name alone: a name reaching here holds no wildcard
    // (see IsPlainHostName).
    WIN32_FIND_DATAW fd;
    HANDLE hFind = ::FindFirstFileW(wide.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return false;
    }
    ::FindClose(hFind);
    return fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK ||
        fd.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT ||
        fd.dwReserved0 == IO_REPARSE_TAG_LX_SYMLINK;
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
