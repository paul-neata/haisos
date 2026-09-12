#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include "interfaces/IFilesystemService.h"

namespace Haisos {

// A view of |main| with |mounted| overlaid at |whereToMount|: paths at or
// under whereToMount are served by mounted (translated to be relative to
// mounted's own root), overriding anything main has there; every other path
// is served by main, unchanged. Neither main nor mounted is mutated -- this
// is a new composite view, not an in-place operation. ReadDirectory on an
// ancestor of whereToMount synthesizes the next path segment towards it as a
// directory entry, even if main has no real directory there, so the mount
// point is always reachable by listing down from the root.
class MountedFileSystem : public IFileSystem {
public:
    MountedFileSystem(std::shared_ptr<IFileSystem> main, const std::string& whereToMount, std::shared_ptr<IFileSystem> mounted);
    ~MountedFileSystem() override;

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
    std::string GetCwd() const;

    std::shared_ptr<IFileSystem> m_main;
    std::string m_mountPoint;
    std::shared_ptr<IFileSystem> m_mounted;
    // This view's own current directory; not delegated to main/mounted (they
    // may be shared by other views/processes). Guarded by m_cwdMutex: this
    // filesystem instance may itself be shared by multiple concurrently-
    // running processes, each on its own thread.
    mutable std::mutex m_cwdMutex;
    std::string m_cwd = "/";

    // main and mounted have independent fd namespaces, so an fd alone doesn't
    // say which one owns it (and the two could coincidentally overlap); this
    // records which side each fd opened through was created on.
    mutable std::mutex m_openFdsMutex;
    std::unordered_set<int> m_mountedFds;
};

}
