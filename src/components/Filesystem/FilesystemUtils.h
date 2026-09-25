#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include "interfaces/IFileSystemService.h"
#include "VirtualPath.h"

#ifdef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace Haisos {

#ifdef _WIN32
constexpr int kFileOpenReadOnly = _O_RDONLY | _O_BINARY;
constexpr int kFileOpenWriteCreateTruncate = _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY;
constexpr int kFileOpenWriteCreateAppend = _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY;
constexpr int kFileCreateMode = _S_IREAD | _S_IWRITE;
constexpr int kFileWriteOnlyBit = _O_WRONLY;
constexpr int kFileReadWriteBit = _O_RDWR;
constexpr int kFileCreateBit = _O_CREAT;
constexpr int kFileTruncateBit = _O_TRUNC;
constexpr int kFileAppendBit = _O_APPEND;
#else
constexpr int kFileOpenReadOnly = O_RDONLY;
constexpr int kFileOpenWriteCreateTruncate = O_WRONLY | O_CREAT | O_TRUNC;
constexpr int kFileOpenWriteCreateAppend = O_WRONLY | O_CREAT | O_APPEND;
constexpr int kFileCreateMode = S_IRUSR | S_IWUSR;
constexpr int kFileWriteOnlyBit = O_WRONLY;
constexpr int kFileReadWriteBit = O_RDWR;
constexpr int kFileCreateBit = O_CREAT;
constexpr int kFileTruncateBit = O_TRUNC;
constexpr int kFileAppendBit = O_APPEND;
#endif

// True if |flags| (as passed to IFileSystem::OpenFile) requests any form of
// write access. Individual bit constants above are platform-specific (see
// IFileSystem's own doc comment); this is the portable way to test them.
inline bool RequestsWriteAccess(int flags) {
    return (flags & kFileWriteOnlyBit) != 0 ||
           (flags & kFileReadWriteBit) != 0 ||
           (flags & kFileCreateBit) != 0 ||
           (flags & kFileTruncateBit) != 0 ||
           (flags & kFileAppendBit) != 0;
}

// Now, as a FileDateTime (the system clock, to the nanosecond where it has them).
inline FileDateTime CurrentFileDateTime() {
    const auto sinceEpoch = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    FileDateTime now;
    now.seconds = static_cast<int64_t>(sinceEpoch / 1000000000);
    now.nanoseconds = static_cast<uint32_t>(sinceEpoch % 1000000000);
    return now;
}

// The 512-byte blocks |size| bytes take up when stored with no waste: what a
// filesystem with no real allocation of its own (an in-memory one) reports.
inline uint64_t BlocksForSize(uint64_t size) {
    return (size + 511) / 512;
}

// What is at |absolutePath| -- DirectoryEntryType::File, ::Dir or
// ::CharDevice -- or
// nullopt if nothing is (see IFileSystem::Stat; a builtin command is a file).
// |fs| is an IFileSystem or a process's IFileIO, as for ReadWholeFile below.
template <typename FileAccess>
inline std::optional<char> EntryTypeOf(FileAccess& fs, const std::string& absolutePath) {
    FileStatus status;
    if (fs.Stat(absolutePath, status) != 0) {
        return std::nullopt;
    }
    return status.type;
}

// Reads the whole file at |path|, up to a 10 MB cap. Returns false on any
// failure. |fs| is anything offering the IFileSystem file operations: an
// IFileSystem (which may be rooted/jailed, and understands absolute paths
// only) or a process's IFileIO (which also resolves a relative path against
// where that process currently is).
template <typename FileAccess>
inline bool ReadWholeFile(FileAccess& fs, const std::string& path, std::string& outContent) {
    int fd = fs.OpenFile(path, kFileOpenReadOnly);
    if (fd < 0) {
        return false;
    }

    constexpr size_t kMaxSize = 10 * 1024 * 1024;
    constexpr size_t kChunkSize = 64 * 1024;
    std::string content;
    char buf[kChunkSize];
    while (content.size() < kMaxSize) {
        ssize_t n = fs.ReadFile(fd, buf, sizeof(buf));
        if (n < 0) {
            fs.CloseFile(fd);
            return false;
        }
        if (n == 0) {
            break;
        }
        content.append(buf, static_cast<size_t>(n));
    }
    fs.CloseFile(fd);
    outContent = std::move(content);
    return true;
}

}
