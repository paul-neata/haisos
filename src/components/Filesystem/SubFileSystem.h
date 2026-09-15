#pragma once
#include <memory>
#include <mutex>
#include <string>
#include "MountableFileSystem.h"
#include "interfaces/IFilesystemService.h"

namespace Haisos {

// A filesystem confined to a sub-path of an existing IFileSystem, addressed
// through the IFileSystem abstraction itself (no real disk access) -- unlike
// PhysicalFileSystem, which jails a real disk directory. Paths are resolved
// lexically against this filesystem's own virtual root, so escaping the sub-path
// (e.g. via "..") is structurally impossible: every resolved path is always
// at or below basePath within root.
class SubFileSystem : public MountableFileSystem {
public:
    SubFileSystem(std::shared_ptr<IFileSystem> root, const std::string& basePath);
    ~SubFileSystem() override;

    int LocalOpenFile(const std::string& pathname, int flags) override;
    int LocalOpenFile(const std::string& pathname, int flags, int mode) override;
    int LocalCloseFile(int fd) override;
    ssize_t LocalReadFile(int fd, void* buf, size_t count) override;
    ssize_t LocalWriteFile(int fd, const void* buf, size_t count) override;

    int LocalCreateDirectory(const std::string& pathname, int mode) override;
    int LocalRemoveDirectory(const std::string& pathname) override;
    int ChangeDirectory(const std::string& path) override;
    char* GetCurrentDirectory(std::string& buf, size_t size) override;

    std::vector<DirectoryEntry> LocalReadDirectory(const std::string& path) override;

    std::string AbsolutePathFor(const std::string& path) const override;
    // A copy of the current directory, taken under m_cwdMutex.
    std::string CwdSnapshot() const;

private:
    std::string ResolveInRoot(const std::string& path) const;

    std::shared_ptr<IFileSystem> m_root;
    std::string m_basePath;
    // This filesystem's own current directory, relative to basePath. Not delegated
    // to root's ChangeDirectory (root may be shared by other composed filesystems and processes).
    // Guarded by m_cwdMutex: this filesystem instance may be shared by
    // multiple concurrently-running processes, each on its own thread.
    mutable std::mutex m_cwdMutex;
    std::string m_cwd = "/";
};

}
