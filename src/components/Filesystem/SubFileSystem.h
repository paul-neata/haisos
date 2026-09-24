#pragma once
#include <memory>
#include <string>
#include "MountableFileSystem.h"
#include "interfaces/IFileSystemService.h"

namespace Haisos {

// A filesystem confined to a sub-path of an existing IFileSystem, addressed
// through the IFileSystem abstraction itself (no real disk access) -- unlike
// PhysicalFileSystem, which jails a real disk directory. Paths are resolved
// lexically against this filesystem's own virtual root, so escaping the sub-path
// (e.g. via "..") is structurally impossible: every resolved path is always
// at or below basePath within root.
class SubFileSystem : public MountableFileSystem {
public:
    static std::shared_ptr<SubFileSystem> Create(std::shared_ptr<IFileSystem> root, const std::string& basePath);
    ~SubFileSystem() override;

    int LocalOpenFile(const std::string& pathname, int flags) override;
    int LocalOpenFile(const std::string& pathname, int flags, int mode) override;
    int LocalCloseFile(int fd) override;
    ssize_t LocalReadFile(int fd, void* buf, size_t count) override;
    ssize_t LocalWriteFile(int fd, const void* buf, size_t count) override;

    int LocalCreateDirectory(const std::string& pathname, int mode) override;
    int LocalRemoveDirectory(const std::string& pathname) override;
    std::vector<DirectoryEntry> LocalReadDirectory(const std::string& path) override;

    std::string AbsolutePathFor(const std::string& path) const override;

private:
    SubFileSystem(std::shared_ptr<IFileSystem> root, const std::string& basePath);

    std::string ResolveInRoot(const std::string& path) const;

    std::shared_ptr<IFileSystem> m_root;
    std::string m_basePath;
};

}
