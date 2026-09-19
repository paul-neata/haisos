#pragma once
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "MountableFileSystem.h"
#include "interfaces/IFilesystemService.h"

namespace Haisos {

// An empty, in-memory, read/write IFileSystem. Files are held as plain byte
// buffers keyed by normalized path; there is no backing real disk. Intended
// for scratch/temporary filesystems and as a mount target (see
// IFilesystemService::CreateComposedFileSystem).
class InMemoryFileSystem : public MountableFileSystem {
public:
    static std::shared_ptr<InMemoryFileSystem> Create();
    ~InMemoryFileSystem() override;

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
    // A copy of the current directory, taken under m_mutex.
    std::string CwdSnapshot() const;

private:
    InMemoryFileSystem();

    struct Node {
        bool isDirectory = false;
        std::vector<char> data;
    };
    struct OpenHandle {
        std::string path;
        size_t position = 0;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, Node> m_nodes;
    std::unordered_map<int, OpenHandle> m_openHandles;
    int m_nextFd = 3;
    std::string m_cwd = "/";
};

}
