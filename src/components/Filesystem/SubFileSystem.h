#pragma once
#include <memory>
#include <string>
#include "interfaces/IFilesystemService.h"

namespace Haisos {

// A view confined to a sub-path of an existing IFileSystem, addressed
// through the IFileSystem abstraction itself (no real disk access) -- unlike
// PhysicalFileSystem, which jails a real disk directory. Paths are resolved
// lexically against this view's own virtual root, so escaping the sub-path
// (e.g. via "..") is structurally impossible: every resolved path is always
// at or below basePath within root.
class SubFileSystem : public IFileSystem {
public:
    SubFileSystem(std::shared_ptr<IFileSystem> root, const std::string& basePath);
    ~SubFileSystem() override;

    int OpenFile(const std::string& pathname, int flags) override;
    int OpenFile(const std::string& pathname, int flags, int mode) override;
    int CloseFile(int fd) override;
    ssize_t ReadFile(int fd, void* buf, size_t count) override;
    ssize_t WriteFile(int fd, const void* buf, size_t count) override;

    int CreateDirectory(const std::string& pathname, int mode) override;
    int RemoveDirectory(const std::string& pathname) override;
    int ChangeDirectory(const std::string& path) override;
    char* GetCurrentDirectory(std::string& buf, size_t size) override;

    std::vector<DirectoryEntry> ReadDirectory(const std::string& path) override;

private:
    std::string ResolveInRoot(const std::string& path) const;

    std::shared_ptr<IFileSystem> m_root;
    std::string m_basePath;
    // This view's own current directory, relative to basePath. Not delegated
    // to root's ChangeDirectory (root may be shared by other views/processes).
    std::string m_cwd = "/";
};

}
