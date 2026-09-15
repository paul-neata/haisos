#pragma once
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "interfaces/IFilesystemService.h"

namespace Haisos {

// The mount table behind IFileSystem::Mount/Unmount, factored out so every
// IFileSystem implementation gets identical routing instead of re-deriving it.
//
// An implementation embeds one of these, and at the top of each path-taking
// method asks Resolve() whether the path belongs to a mounted filesystem; if it
// does, the call is forwarded there and the implementation's own storage is
// never touched. That is what makes mounting delegation rather than projection:
// a PhysicalFileSystem mounted into an InMemoryFileSystem still writes to disk.
//
// Paths are matched in whatever normalized absolute form the host filesystem
// already uses for its own address space, so each implementation normalizes
// first and then asks.
class MountPoints {
public:
    struct Route {
        // Null when the path is not under any mount point.
        IFileSystem* filesystem = nullptr;
        // The path rewritten to be relative to that filesystem's own root.
        std::string innerPath;
    };

    // A mounted filesystem hands out fds from its own namespace, which routinely
    // overlaps the host's (in-memory fds start at 3, and so do real POSIX ones).
    // Fds for mounted files are therefore re-issued from a range far above any
    // real descriptor, so a host fd is passed through untouched and the two can
    // never be confused.
    static constexpr int kSyntheticFdBase = 1 << 20;
    static bool IsSynthetic(int fd) { return fd >= kSyntheticFdBase; }

    void Mount(const std::string& path, std::shared_ptr<IFileSystem> filesystem);
    // Removes the mount at exactly this path. Unmounting a path that is not a
    // mount point does nothing.
    void Unmount(const std::string& path);

    bool Empty() const;

    // Longest mount point first, so nested mounts resolve to the innermost one.
    Route Resolve(const std::string& path) const;

    // Wraps an fd returned by a mounted filesystem. Passes negative values
    // (errors) straight through.
    int RegisterFd(IFileSystem* filesystem, int innerFd);
    bool LookupFd(int fd, IFileSystem*& filesystem, int& innerFd) const;
    void ReleaseFd(int fd);

    // The next path segment towards each mount point under |directory|, so a
    // listing can show the way down to a mount even when the host filesystem has
    // no real directory there.
    std::vector<std::string> ChildSegments(const std::string& directory) const;

private:
    struct Entry {
        std::string path;
        std::shared_ptr<IFileSystem> filesystem;
    };

    struct Handle {
        IFileSystem* filesystem = nullptr;
        int innerFd = -1;
    };

    mutable std::mutex m_mutex;
    std::vector<Entry> m_mounts;
    std::unordered_map<int, Handle> m_openFds;
    int m_nextFd = kSyntheticFdBase;
};

}
