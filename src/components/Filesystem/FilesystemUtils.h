#pragma once
#include <string>
#include "interfaces/IFilesystemService.h"

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

// Reads the whole file at |path| (through |fs|, which may be a rooted/jailed
// filesystem), up to a 10 MB cap. Returns false on any failure.
inline bool ReadWholeFile(IFileSystem& fs, const std::string& path, std::string& outContent) {
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
