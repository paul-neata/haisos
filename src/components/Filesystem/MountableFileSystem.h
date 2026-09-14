#pragma once
#include <string>
#include "MountPoints.h"
#include "interfaces/IFilesystemService.h"

namespace Haisos {

// Implements IFileSystem's mount routing once, so every filesystem supports
// Mount/Unmount identically instead of six classes each re-deriving it (and
// each re-deriving the file-descriptor translation a mount needs).
//
// A subclass implements the Local* operations, which see only the paths this
// filesystem itself owns: anything at or under a mount point has already been
// routed away by the time they are called. ChangeDirectory/GetCurrentDirectory
// are deliberately not routed -- the current directory is this filesystem's own
// state, and a relative path is resolved against it before being matched, so a
// cwd inside a mounted subtree still works.
class MountableFileSystem : public IFileSystem {
public:
    void Mount(const std::string& whereToMount, std::shared_ptr<IFileSystem> toBeMounted) final;
    void Unmount(const std::string& mountedPath) final;

    int OpenFile(const std::string& pathname, int flags) final;
    int OpenFile(const std::string& pathname, int flags, int mode) final;
    int CloseFile(int fd) final;
    ssize_t ReadFile(int fd, void* buf, size_t count) final;
    ssize_t WriteFile(int fd, const void* buf, size_t count) final;
    int CreateDirectory(const std::string& pathname, int mode) final;
    int RemoveDirectory(const std::string& pathname) final;
    std::vector<DirectoryEntry> ReadDirectory(const std::string& path) final;

protected:
    // How this filesystem addresses |path| absolutely (resolved against its own
    // current directory, if it has one). Mount points are matched against this,
    // so a subclass and its mounts agree on what a path means.
    virtual std::string AbsolutePathFor(const std::string& path) const = 0;

    virtual int LocalOpenFile(const std::string& pathname, int flags) = 0;
    virtual int LocalOpenFile(const std::string& pathname, int flags, int mode) = 0;
    virtual int LocalCloseFile(int fd) = 0;
    virtual ssize_t LocalReadFile(int fd, void* buf, size_t count) = 0;
    virtual ssize_t LocalWriteFile(int fd, const void* buf, size_t count) = 0;
    virtual int LocalCreateDirectory(const std::string& pathname, int mode) = 0;
    virtual int LocalRemoveDirectory(const std::string& pathname) = 0;
    virtual std::vector<DirectoryEntry> LocalReadDirectory(const std::string& path) = 0;

private:
    MountPoints m_mounts;
};

}
