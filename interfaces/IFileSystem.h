#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstddef>

#ifdef _WIN32
using ssize_t = std::ptrdiff_t;
#endif

namespace Haisos {

enum DirectoryEntryType : char {
    File = 'f',
    Dir = 'd'
};

struct DirectoryEntry {
    std::string name;
    char type;
};

// IFileSystem is a thin abstraction over C/POSIX filesystem operations.
//
// Mode values:
//   The |mode| parameter is a platform-specific permission bitmask passed to the
//   underlying C function. On Linux it uses POSIX permission bits (e.g. S_IRUSR,
//   S_IWUSR, S_IRWXU). On Windows it uses the CRT constants (e.g. _S_IREAD,
//   _S_IWRITE). Callers should use the macros provided by the platform headers.
//
// Flags values:
//   The |flags| parameter is a platform-specific bitmask passed to the underlying
//   C open function. On Linux it uses POSIX open flags (e.g. O_RDONLY, O_WRONLY,
//   O_RDWR, O_CREAT, O_TRUNC). On Windows it uses the CRT constants (e.g.
//   _O_RDONLY, _O_WRONLY, _O_RDWR, _O_CREAT, _O_TRUNC). Callers should use the
//   macros provided by the platform headers.
class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    // OpenFile is the IFileSystem counterpart of the C open() function.
    virtual int OpenFile(const std::string& pathname, int flags) = 0;

    // OpenFile is the IFileSystem counterpart of the C open() function (with mode).
    virtual int OpenFile(const std::string& pathname, int flags, int mode) = 0;

    // CloseFile is the IFileSystem counterpart of the C close() function.
    virtual int CloseFile(int fd) = 0;

    // ReadFile is the IFileSystem counterpart of the C read() function.
    virtual ssize_t ReadFile(int fd, void* buf, size_t count) = 0;

    // WriteFile is the IFileSystem counterpart of the C write() function.
    virtual ssize_t WriteFile(int fd, const void* buf, size_t count) = 0;

    // CreateDirectory is the IFileSystem counterpart of the C mkdir() function.
    virtual int CreateDirectory(const std::string& pathname, int mode) = 0;

    // RemoveDirectory is the IFileSystem counterpart of the C rmdir() function.
    virtual int RemoveDirectory(const std::string& pathname) = 0;

    // ChangeDirectory is the IFileSystem counterpart of the C chdir() function.
    virtual int ChangeDirectory(const std::string& path) = 0;

    // GetCurrentDirectory is the IFileSystem counterpart of the C getcwd() function.
    virtual char* GetCurrentDirectory(std::string& buf, size_t size) = 0;

    // ReadDirectory returns the non-"." / ".." entries in |path|.
    // There is no direct single C counterpart; it wraps opendir/readdir/closedir
    // on POSIX and FindFirstFile/FindNextFile on Windows.
    // Note: on POSIX, when d_type is unknown, stat() is used rather than lstat(),
    // so symbolic links are followed and the target's type is reported (not the
    // symlink type itself).
    virtual std::vector<DirectoryEntry> ReadDirectory(const std::string& path) = 0;
};

std::unique_ptr<IFileSystem> CreateFilesystem();

} // namespace Haisos
