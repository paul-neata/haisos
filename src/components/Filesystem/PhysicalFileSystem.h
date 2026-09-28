#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "MountableFileSystem.h"
#include "interfaces/IFileSystemService.h"
#include "Filesystem.h"

namespace Haisos {

// A FileSystem jailed to a real disk directory: every path passed in is
// resolved and validated to stay within rootPath before being forwarded to the
// real filesystem (a request that would escape rootPath via ".." fails).
//
// rootPath is a physical path (see PhysicalPath.h): absolute or relative (to
// the current directory), and on Windows written with '\' or '/', as a drive
// path (c:\x), a UNC one (\\server\share\x), or a path of the full physical
// filesystem (/c/x is C:\x). "/" itself there is the full physical filesystem,
// WindowsFullPhysicalFileSystem, which Create returns for it. A root naming no
// directory at all is logged, and every call on the filesystem then fails.
//
// A path handed to it is taken segment by segment, the way the mounts and
// builtins over it take it (VirtualPath.h: '\' separates too on Windows), and
// each segment must reach the host as exactly that name (IsPlainHostName): on
// Windows no "c:", "bin." or "con", which would lead somewhere else than the
// path says. Names are UTF-8, as everywhere in Haisos.
//
// Symbolic links already on the disk are followed only while they stay within
// the root, and a path ending in a link that leads nowhere (a dangling one) is
// refused: opening it with O_CREAT would create whatever it points at, perhaps
// outside the root. Removing and creating directories never follow a link at
// the end of the path -- a link is removed itself, as unlink() and rmdir() do
// -- and Stat says when a path is one (FileStatus::symbolicLink). On Windows a
// junction is a link too. The root itself can be neither removed nor created.
// Nothing done through here can create a link.
//
// It has no current directory: every path is resolved against rootPath, so
// "foo" and "/foo" both mean rootPath/foo. Where a Haisos process considers
// itself to be is the process's own business (see ICurrentProcess).
class PhysicalFileSystem : public MountableFileSystem {
public:
    static std::shared_ptr<PhysicalFileSystem> Create(const std::string& rootPath);
    ~PhysicalFileSystem() override = default;

    int LocalOpenFile(const std::string& pathname, int flags) override;
    int LocalOpenFile(const std::string& pathname, int flags, int mode) override;
    int LocalCloseFile(int fd) override;
    ssize_t LocalReadFile(int fd, void* buf, size_t count) override;
    ssize_t LocalWriteFile(int fd, const void* buf, size_t count) override;

    int LocalCreateDirectory(const std::string& pathname, int mode) override;
    int LocalRemoveDirectory(const std::string& pathname) override;
    int LocalRemoveFile(const std::string& pathname) override;
    std::vector<DirectoryEntry> LocalReadDirectory(const std::string& path) override;
    int LocalStat(const std::string& path, FileStatus& out) override;

    std::string AbsolutePathFor(const std::string& path) const override;

protected:
    // For a filesystem that places its paths on the host some other way than
    // under one root directory (WindowsFullPhysicalFileSystem): it overrides
    // HostPathOf and IsWithin, and has no root of its own.
    PhysicalFileSystem();

    // Where the path made of |segments| (already split, "." and ".."
    // resolved, each a plain host name) lies on the host, joined without
    // consulting the disk. No segments is the top of this filesystem. Returns
    // false if the path lies nowhere on the host.
    virtual bool HostPathOf(const std::vector<std::string>& segments, std::filesystem::path& hostPath) const;
    // Whether |canonical|, a canonical host path in UTF-8, lies within this
    // filesystem.
    virtual bool IsWithin(const std::string& canonical) const;

private:
    explicit PhysicalFileSystem(const std::string& rootPath);

    // What ResolveOnHost does with the last component of a path.
    enum class LastComponent {
        // Follows it, as open() and stat() do: a symbolic link there is
        // resolved, and must lead somewhere within the filesystem.
        Follow,
        // Takes it as it is, as unlink(), rmdir() and mkdir() do: only the
        // directory holding it is resolved, so a link there is acted on
        // itself, never what it points at.
        Keep,
    };

    // The one place a path handed to this filesystem becomes a real one:
    // splits |pathname|, checks every segment, places it on the host
    // (HostPathOf) and resolves it there as |last| says, making sure it stays
    // within (IsWithin). Returns false (leaving |resolved| untouched) if the
    // path is invalid, climbs above the top, lies outside, or ends in a
    // dangling link. |resolved| is a UTF-8 host path.
    bool ResolveOnHost(const std::string& pathname, LastComponent last, std::string& resolved) const;
    // |hostPath| canonicalized, if the result lies within the filesystem and
    // does not end in a link (which, once canonicalized, means a dangling
    // one).
    bool CanonicalWithin(const std::filesystem::path& hostPath, const std::string& pathname, std::string& resolved) const;
    // Whether the last component of |pathname| is itself a link.
    bool IsSymbolicLink(const std::string& pathname) const;

    // The root, a canonical host path in UTF-8; empty if there is none.
    std::string m_rootPath;
    std::shared_ptr<FileSystem> m_inner;
};

} // namespace Haisos
