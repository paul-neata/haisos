#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>

#ifdef _WIN32
using ssize_t = std::ptrdiff_t;
#endif

namespace Haisos {

enum DirectoryEntryType : char {
    File = 'f',
    Dir = 'd',
    // A character device, such as /dev/null: a file whose reads and writes are
    // served by a driver rather than stored anywhere (see
    // IFileSystemService::CreateDeviceFileSystem).
    CharDevice = 'c'
};

struct DirectoryEntry {
    std::string name;
    char type;
};

// A point in time, as POSIX's struct timespec: whole seconds since the Unix
// epoch (1970-01-01 00:00:00 UTC) plus the nanoseconds into that second. It is
// always UTC; turning it into a local date is the reader's business. Compares
// chronologically.
struct FileDateTime {
    int64_t seconds = 0;
    uint32_t nanoseconds = 0; // 0 to 999999999

    bool operator==(const FileDateTime& other) const {
        return seconds == other.seconds && nanoseconds == other.nanoseconds;
    }
    bool operator!=(const FileDateTime& other) const { return !(*this == other); }
    bool operator<(const FileDateTime& other) const {
        return seconds != other.seconds ? seconds < other.seconds : nanoseconds < other.nanoseconds;
    }
    bool operator>(const FileDateTime& other) const { return other < *this; }
    bool operator<=(const FileDateTime& other) const { return !(other < *this); }
    bool operator>=(const FileDateTime& other) const { return !(*this < other); }
};

// What IFileSystem::Stat reports about a path: the part of POSIX's struct stat
// that means something on every filesystem here. There are no permissions or
// owners yet, so they are not pretended to.
struct FileStatus {
    char type = DirectoryEntryType::File; // DirectoryEntryType::File, ::Dir or ::CharDevice
    // In bytes. For a directory, whatever the filesystem reports (a real disk
    // typically says 4096; an in-memory directory is 0).
    uint64_t size = 0;
    // Storage allocated, in 512-byte blocks, as st_blocks (what `ls -s` and
    // the `total` line of `ls -l` count).
    uint64_t blocks = 0;
    // Hard links, as st_nlink: 1 for a file, and for a directory 2 plus one
    // per subdirectory (its own "." and each child's "..").
    uint64_t linkCount = 1;
    // As st_atim, st_mtim and st_ctim: last read, last change of content, and
    // last change of content or status. All zero when the filesystem cannot
    // say. (On Windows, changeTime is the creation time, as _stat64 reports.)
    FileDateTime accessTime;
    FileDateTime modificationTime;
    FileDateTime changeTime;
    // As st_rdev: for a device, its major and minor numbers (Linux's, e.g.
    // 1 and 3 for null); 0 for anything else.
    uint32_t deviceMajor = 0;
    uint32_t deviceMinor = 0;
    // Whether the path itself -- its last component, before it is followed --
    // is a symbolic link: what lstat() would add. Everything else here
    // describes what the link leads to, as stat() does, so a link to a
    // directory has the type Dir; this is how a caller walking a tree (the
    // haisosfile's DELETE) tells it apart, to remove the link instead of
    // descending into it. Only PhysicalFileSystem has links to report; every
    // other filesystem leaves it false, and one composing others (read-only,
    // sub-path, composed, mounted) passes on what the one serving the path
    // says.
    bool symbolicLink = false;
};

// IFileSystem is a thin abstraction over C/POSIX filesystem operations.
//
// A filesystem has no current directory of its own: that notion belongs to a
// process (see ICurrentProcess::ChangeDirectory). Every path handed to an
// IFileSystem is therefore resolved against the filesystem's own root, so
// "foo" and "/foo" mean the same thing. A process that wants "relative to
// where I am" resolves the path against its own working directory first.
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
    // Like it, it does not follow a symbolic link at the end of |pathname|: a
    // link there, even a dangling one, means the path exists already.
    virtual int CreateDirectory(const std::string& pathname, int mode) = 0;

    // RemoveDirectory is the IFileSystem counterpart of the C rmdir() function.
    // It never removes a directory through a symbolic link at the end of
    // |pathname|: on POSIX it fails on one, and on Windows it removes a
    // directory link itself, as rmdir() does there.
    virtual int RemoveDirectory(const std::string& pathname) = 0;

    // RemoveFile is the IFileSystem counterpart of the C unlink() function: it
    // removes a file, never a directory (use RemoveDirectory for those). A
    // symbolic link at the end of |pathname| is removed itself, never what it
    // points at.
    virtual int RemoveFile(const std::string& pathname) = 0;

    // Mount makes |toBeMounted| serve every path at or under |whereToMount| on
    // THIS filesystem, in place -- unlike IFileSystemService::CreateComposedFileSystem,
    // which leaves its operands alone and returns a new filesystem. Calls are
    // routed to whichever filesystem owns a path, and that filesystem decides
    // what they mean, so mounting a disk-backed filesystem into an in-memory one
    // still writes those paths to disk. Mounting over an existing mount point
    // replaces it. The mount point need not exist: listing an ancestor shows the
    // way down to it regardless.
    virtual void Mount(const std::string& whereToMount, std::shared_ptr<IFileSystem> toBeMounted) = 0;

    // Removes the mount at exactly |mountedPath|. Unmounting a path that is not
    // a mount point does nothing. Files still open on the unmounted filesystem
    // keep working until they are closed.
    virtual void Unmount(const std::string& mountedPath) = 0;

    // ReadDirectory returns the entries in the directory |path|: "." and ".."
    // first, as readdir() gives them, then everything else. A path that is no
    // directory lists nothing at all. (The ".." of the root is the root.)
    // There is no direct single C counterpart; it wraps opendir/readdir/closedir
    // on POSIX and FindFirstFile/FindNextFile on Windows.
    // Note: on POSIX, when d_type is unknown, stat() is used rather than lstat(),
    // so symbolic links are followed and the target's type is reported (not the
    // symlink type itself): a link to a directory lists as a directory. Stat
    // tells the two apart (FileStatus::symbolicLink).
    virtual std::vector<DirectoryEntry> ReadDirectory(const std::string& path) = 0;

    // Stat is the IFileSystem counterpart of the C stat() function: fills
    // |out| with what is at |path| and returns 0, or returns -1 (leaving |out|
    // untouched) if nothing is there. Symbolic links on a real disk are
    // followed, and FileStatus::symbolicLink says whether |path| was one. A
    // mount point, and every directory on the way down to one,
    // is a directory even where the filesystem underneath has none, timed from
    // when the mount was made; a builtin command is a file the size of its
    // note, taking no storage (0 blocks), timed from when it was placed.
    virtual int Stat(const std::string& path, FileStatus& out) = 0;

    // --- Builtin commands (see IBuiltinCommands) ---
    //
    // A builtin command is a program compiled into Haisos itself, placed on a
    // filesystem at a path so that starting that path runs it. Every
    // filesystem keeps its own list of the builtins placed on it. On that
    // filesystem, a builtin's path:
    //   * lists as a file in its directory;
    //   * reads as a short text naming the builtin, but can never be opened
    //     for writing, created over, or removed with RemoveFile;
    //   * pins its directory -- and every directory above it -- in place:
    //     RemoveDirectory refuses any of them while the builtin is there.
    //
    // Placing one is how an OS is assembled, not something a running program
    // may do, so like Mount these are not on IFileIO. IBuiltinConfigurator is
    // the front door onto them, adding the checks a placement needs.

    // Places builtinName at |path| in this filesystem's own list. Returns 0, or
    // -1 if |path| is the root or already holds a builtin of this filesystem,
    // or if this filesystem holds nothing but what it was made with (a
    // device filesystem).
    virtual int AddBuiltinCommand(const std::string& path, const std::string& builtinName) = 0;

    // Removes the builtin this filesystem itself placed at |path|. Returns 0,
    // or -1 if its own list has none there (a builtin seen through another
    // filesystem is that one's to remove).
    virtual int RemoveBuiltinCommand(const std::string& path) = 0;

    // The name of the builtin at |path|, or nullopt if there is none. This
    // filesystem's own list is consulted first; failing that, whichever
    // filesystem serves |path| underneath -- a mount, or the one this
    // filesystem is a view of (read-only, sub-path, composed) -- is asked.
    virtual std::optional<std::string> IsBuiltinCommand(const std::string& path) = 0;
};

// A factory for composing filesystems. Every method returns a new,
// independent view; none of them mutate the filesystems passed in.
class IFileSystemService {
public:
    virtual ~IFileSystemService() = default;

    // Wraps an existing filesystem, allowing only read/navigation operations
    // (every write, create, or remove is rejected).
    virtual std::shared_ptr<IFileSystem> CreateReadOnlyFileSystem(std::shared_ptr<IFileSystem> filesystem) = 0;

    // Creates an empty, in-memory read/write filesystem (no backing real disk).
    virtual std::shared_ptr<IFileSystem> CreateEmptyInMemFileSystem() = 0;

    // Creates a device filesystem, as Linux's /dev: a root directory holding
    // character devices, and nothing can be created in it or removed from it.
    // The devices, which behave as their Linux namesakes:
    //   null  writes are discarded; reads return end-of-file at once
    //   zero  writes are discarded; reads return as many 0 bytes as asked for
    // Meant to be mounted at /dev.
    virtual std::shared_ptr<IFileSystem> CreateDeviceFileSystem() = 0;

    // Creates a filesystem confined to a sub-path of an existing filesystem, working
    // purely through the IFileSystem abstraction (no real disk access, unlike
    // IFactory::CreatePhysicalFileSystem).
    virtual std::shared_ptr<IFileSystem> CreateSubFileSystem(std::shared_ptr<IFileSystem> root, const std::string& path) = 0;

    // Creates a new filesystem composing `main` with `toBeMounted` overlaid at
    // `whereToMount`: paths at or under whereToMount are served by
    // toBeMounted, overriding anything main has there; the mount point is
    // synthesized as a directory on listing even if main has none there.
    // Neither main nor toBeMounted is mutated.
    virtual std::shared_ptr<IFileSystem> CreateComposedFileSystem(std::shared_ptr<IFileSystem> main, const std::string& whereToMount, std::shared_ptr<IFileSystem> toBeMounted) = 0;
};

std::shared_ptr<IFileSystem> CreateFilesystem();

} // namespace Haisos
