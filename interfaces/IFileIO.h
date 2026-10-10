#pragma once
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "IFileDescriptor.h"
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

    // The standard slots of the descriptor table, and its size (as RLIMIT_NOFILE).
    static constexpr int kStdIn = 0;
    static constexpr int kStdOut = 1;
    static constexpr int kStdErr = 2;
    static constexpr int kMaxDescriptors = 1024;

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
    // IFileSystem for what |flags| and |mode| mean); the only difference is
    // that a relative path is allowed here. OpenFile hands back the open file
    // itself -- the IFileSystem counterpart's result -- without numbering it;
    // the descriptor table below is what gives a file a number, and only what
    // is placed there is released when the process's program ends.
    // A failure to reach the OS at all is reported the same way as any other
    // failure: a null descriptor, a negative result, or an empty listing.

    // As open(), resolving |pathname| against the working directory: the open
    // file, or null on failure. It is NOT placed in the descriptor table; pass it
    // to AddDescriptor (or Dup2 it in) to give it a number.
    virtual std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags) = 0;
    virtual std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags, int mode) = 0;
    virtual int CreateDirectory(const std::string& pathname, int mode) = 0;
    virtual int RemoveDirectory(const std::string& pathname) = 0;
    virtual int RemoveFile(const std::string& pathname) = 0;
    // As rename(): both paths resolved against the working directory; see
    // IFileSystem::Rename, including kFileSystemCrossDevice across filesystems.
    virtual int Rename(const std::string& oldPath, const std::string& newPath) = 0;
    // As utimensat(): see IFileSystem::SetTimes.
    virtual int SetTimes(const std::string& path,
                         const std::optional<FileDateTime>& accessTime,
                         const std::optional<FileDateTime>& modificationTime) = 0;
    virtual std::vector<DirectoryEntry> ReadDirectory(const std::string& path) = 0;
    virtual int Stat(const std::string& path, FileStatus& out) = 0;

    // The name of the builtin command at |path| (resolved like every path
    // here), or nullopt if it is not one -- see IFileSystem::IsBuiltinCommand.
    // Only asking is possible from a process: placing a builtin is how an OS is
    // assembled (see IBuiltinConfigurator), for the same reason Mount is not
    // here either.
    virtual std::optional<std::string> IsBuiltinCommand(const std::string& path) = 0;

    // --- The descriptor table ---
    // The process's open files by number, as a POSIX process holds them: 0 stdin,
    // 1 stdout, 2 stderr, then 3 and up, at most kMaxDescriptors slots. A slot
    // holds a shared_ptr, so several slots (and several processes) may hold the
    // same open file -- one position, closed when the last holder releases it.
    // Every one is released when the process's program ends.

    // The descriptor in slot |fd|, or null if the slot is empty or out of range.
    virtual std::shared_ptr<IFileDescriptor> GetDescriptor(int fd) const = 0;
    // Places |descriptor| in the lowest free slot and returns it; -1 if the table
    // is full or |descriptor| is null.
    virtual int AddDescriptor(std::shared_ptr<IFileDescriptor> descriptor) = 0;
    // As dup(): places slot |fd|'s descriptor in the lowest free slot as well and
    // returns it; -1 if |fd| is empty or out of range, or the table is full.
    virtual int Dup(int fd) = 0;
    // As dup2(): makes slot |newFd| hold slot |oldFd|'s descriptor, releasing
    // what |newFd| held, in one step; returns newFd. -1 if |oldFd| is empty or
    // out of range or |newFd| is out of range (then nothing changes). With
    // oldFd == newFd and oldFd holding a descriptor, returns newFd and changes nothing.
    virtual int Dup2(int oldFd, int newFd) = 0;
    // As close(): empties slot |fd|, releasing its descriptor; 0, or -1 if it was
    // empty or out of range.
    virtual int CloseDescriptor(int fd) = 0;

    // As pipe(): makes a pipe through the OS's pipe service (see IPipeService)
    // and places its read end and write end in the two lowest free slots, read end
    // first. Returns {readSlot, writeSlot}, or nullopt -- the table left as it was
    // -- when fewer than two slots are free or the OS is gone. capacity 0 is
    // kDefaultPipeCapacity.
    virtual std::optional<std::pair<int, int>> CreatePipe(size_t capacity = 0) = 0;

    // Deliberately NOT here, though IFileSystem has them: Mount and Unmount.
    // Composing filesystems is how an OS is built, not something a program
    // running inside one may do to the ground it stands on -- a process that
    // could mount would be a process that could widen its own reach, which is
    // exactly what ICurrentProcess exists to prevent. They stay on IFileSystem,
    // reachable only by whoever assembles an OS.
};

}
