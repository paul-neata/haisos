#pragma once
#include <memory>
#include <string>
#include <vector>
#include "IFileSystemService.h"

namespace Haisos {

// How a running process does file I/O: the operations of IFileSystem, bound to
// the root filesystem of the OS that process was given (ICurrentProcess::OS()),
// plus the working directory those operations resolve against.
//
// This is the object a process actually reaches for -- the tools an agent calls
// and the globals a Lua script gets all go through ICurrentProcess::IO(), never
// to an IFileSystem directly. The difference is the one that matters in
// practice: an IFileSystem understands only absolute paths, while this resolves
// "notes.txt" against where the process currently is, and "../shared/notes.txt"
// likewise. Handing a program a raw IFileSystem would mean every caller
// re-deriving that, and getting it wrong differently each time.
//
// Every process has its own, so one process moving directory never moves
// another; a filesystem deliberately holds no current directory of its own
// (see IFileSystem).
class IFileIO {
public:
    virtual ~IFileIO() = default;

    // --- Where the process is ---

    // Always an absolute path within the OS's root; "/" is that root.
    virtual std::string GetCurrentDirectory() const = 0;

    // Moves the working directory, resolving path against the current one.
    // Returns 0 on success and -1 if the target is not a directory of the OS's
    // root filesystem, matching chdir(). Escaping the root is not possible:
    // ".." at "/" stays at "/".
    virtual int ChangeDirectory(const std::string& path) = 0;

    // Resolves a path the way this process means it -- against the working
    // directory, unless it is already absolute -- giving the absolute path the
    // underlying filesystem will see. Every operation below does this for
    // itself; it is exposed for diagnostics and for callers that need to
    // report or compare the path actually used.
    virtual std::string ResolvePath(const std::string& path) const = 0;

    // --- The IFileSystem operations, on resolved paths ---
    // Each is the counterpart of the IFileSystem method of the same name (see
    // IFileSystem for what |flags| and |mode| mean, and for the file-descriptor
    // contract); the only difference is that a relative path is allowed here.
    // A failure to reach the OS at all is reported the same way as any other
    // failure: a negative result, or an empty listing.
    virtual int OpenFile(const std::string& pathname, int flags) = 0;
    virtual int OpenFile(const std::string& pathname, int flags, int mode) = 0;
    virtual int CloseFile(int fd) = 0;
    virtual ssize_t ReadFile(int fd, void* buf, size_t count) = 0;
    virtual ssize_t WriteFile(int fd, const void* buf, size_t count) = 0;
    virtual int CreateDirectory(const std::string& pathname, int mode) = 0;
    virtual int RemoveDirectory(const std::string& pathname) = 0;
    virtual int RemoveFile(const std::string& pathname) = 0;
    virtual std::vector<DirectoryEntry> ReadDirectory(const std::string& path) = 0;

    // Deliberately NOT here, though IFileSystem has them: Mount and Unmount.
    // Composing filesystems is how an OS is built, not something a program
    // running inside one may do to the ground it stands on -- a process that
    // could mount would be a process that could widen its own reach, which is
    // exactly what ICurrentProcess exists to prevent. They stay on IFileSystem,
    // reachable only by whoever assembles an OS.
};

}
