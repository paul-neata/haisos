#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include "interfaces/IFilesystemService.h"

namespace Haisos {

// A composition of |main| with |mounted| overlaid at |whereToMount|: paths at or
// under whereToMount are served by mounted (translated to be relative to
// mounted's own root), overriding anything main has there; every other path
// is served by main, unchanged. Neither main nor mounted is mutated -- this
// is a new composed filesystem, not an in-place operation. ReadDirectory on an
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
    // Which of the two underlying filesystems an open file lives on.
    enum class Side { Main, Mounted };

    // What a synthetic fd handed out by this filesystem actually refers to.
    struct Handle {
        Side side = Side::Main;
        int innerFd = -1;
    };

    // The first fd this view hands out; above the standard streams (0/1/2).
    static constexpr int kFirstSyntheticFd = 3;

    std::string GetCwd() const;

    const std::shared_ptr<IFileSystem>& FileSystemFor(Side side) const;
    // Takes the fd an underlying filesystem returned and gives back the
    // synthetic fd callers of this filesystem see (or the underlying error value).
    int RegisterFd(Side side, int innerFd);
    // Returns false if |fd| was not handed out by this filesystem (or is already
    // closed); otherwise fills in |handle|.
    bool LookupFd(int fd, Handle& handle) const;
    // Returns the next synthetic fd that is not currently live, or -1 if there
    // is none. m_openFdsMutex must be held.
    int AllocateFdLocked();

    std::shared_ptr<IFileSystem> m_main;
    std::string m_mountPoint;
    std::shared_ptr<IFileSystem> m_mounted;
    // This filesystem's own current directory; not delegated to main/mounted (they
    // may be shared by other composed filesystems and processes). Guarded by m_cwdMutex: this
    // filesystem instance may itself be shared by multiple concurrently-
    // running processes, each on its own thread.
    mutable std::mutex m_cwdMutex;
    std::string m_cwd = "/";

    // main and mounted have independent fd namespaces that routinely overlap
    // (InMemoryFileSystem hands out fds from 3 up, and PhysicalFileSystem
    // returns real OS fds, which also start around 3), so an underlying fd
    // alone cannot say which side owns it. This filesystem therefore hands out fds
    // from its own namespace and maps each one to the side it was opened on
    // plus the fd that side returned; every fd-taking method translates back
    // before dispatching.
    mutable std::mutex m_openFdsMutex;
    std::unordered_map<int, Handle> m_openFds;
    // Synthetic fds are handed out in increasing order, so one is never reused
    // while it is still live.
    int m_nextFd = kFirstSyntheticFd;
};

}
