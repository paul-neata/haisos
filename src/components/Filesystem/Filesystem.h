#pragma once
#include "MountableFileSystem.h"
#include "VirtualPath.h"
#include "interfaces/IFileSystemService.h"

namespace Haisos {

// The host's own file calls, on host paths, unrooted: whatever path it is
// handed is where it goes. Paths and names are UTF-8 on every platform; on
// Windows they reach the host through its UTF-16 ("W") calls, so any name
// works there, not only those of the ANSI code page. Every call runs with a
// NoCriticalErrorDialogs in scope.
class FileSystem : public MountableFileSystem {
public:
    static std::shared_ptr<FileSystem> Create() {
        return std::shared_ptr<FileSystem>(new FileSystem());
    }
    ~FileSystem() override = default;

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
    std::string AbsolutePathFor(const std::string& path) const override {
        return NormalizeVirtualPath(path, "/");
    }

    // Whether the host path |hostPath| is itself a link, not followed: a
    // symbolic link, or on Windows also a junction or a symbolic link made
    // by WSL -- which Windows cannot follow, and std::filesystem does not
    // report as links. False if there is nothing at |hostPath|.
    static bool IsLink(const std::string& hostPath);

    // Whether the host takes |hostPath| for a device rather than a file: on
    // Windows, one ending in a legacy device name (NUL, CON, COM1, ...), as
    // GetFullPathNameW says -- the very conversion CreateFileW makes. Which
    // names those are varies between Windows versions: Windows 10 takes
    // NUL.txt for the null device, Windows 11 for a file. Never elsewhere.
    static bool IsDevicePath(const std::string& hostPath);

private:
    FileSystem() = default;
};

} // namespace Haisos
