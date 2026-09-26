#pragma once
#include <filesystem>
#include <string>
#include "MountableFileSystem.h"
#include "interfaces/IFileSystemService.h"
#include "Filesystem.h"

namespace Haisos {

// A FileSystem jailed to a real disk directory: every path passed in is
// resolved and validated to stay within rootPath before being forwarded to the
// real filesystem (a request that would escape rootPath via ".." fails).
//
// Symbolic links already on the disk are followed only while they stay within
// the root, and a path ending in a link that leads nowhere (a dangling one) is
// refused: opening it with O_CREAT would create whatever it points at, perhaps
// outside the root. Removing and creating directories never follow a link at
// the end of the path -- a link is removed itself, as unlink() and rmdir() do
// -- and Stat says when a path is one (FileStatus::symbolicLink). The root
// itself can be neither removed nor created. Nothing done through here can
// create a link.
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

private:
    explicit PhysicalFileSystem(const std::string& rootPath);

    // What ResolveWithinRoot does with the last component of a path.
    enum class LastComponent {
        // Follows it, as open() and stat() do: a symbolic link there is
        // resolved, and must lead somewhere within the root.
        Follow,
        // Takes it as it is, as unlink(), rmdir() and mkdir() do: only the
        // directory holding it is resolved, so a link there is acted on
        // itself, never what it points at.
        Keep,
    };

    // The one place a path handed to this filesystem becomes a real one:
    // resolves |pathname| against the root, as |last| says, and validates that
    // it does not escape it. Returns false (leaving |resolved| untouched) if
    // the path is invalid, escapes the root, or ends in a dangling link.
    bool ResolveWithinRoot(const std::string& pathname, LastComponent last, std::string& resolved) const;
    // |pathname| joined to the root, "." and ".." resolved lexically: "/foo"
    // and "foo" alike are rootPath/foo. Nothing on disk is consulted.
    std::filesystem::path JoinUnderRoot(const std::string& pathname) const;
    // |joined| canonicalized, if the result lies within the root and does not
    // end in a symbolic link (which, once canonicalized, means a dangling one).
    bool CanonicalWithinRoot(const std::filesystem::path& joined, const std::string& pathname, std::string& resolved) const;
    // Whether the last component of |pathname| is itself a symbolic link.
    bool IsSymbolicLink(const std::string& pathname) const;

    std::string m_rootPath;
    std::shared_ptr<FileSystem> m_inner;
};

} // namespace Haisos
