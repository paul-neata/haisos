#pragma once
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "FilesystemUtils.h"
#include "MountableFileSystem.h"
#include "interfaces/IFileSystemService.h"

namespace Haisos {

class InMemoryFileDescriptor;

// An empty, in-memory, read/write IFileSystem. Files are held as plain byte
// buffers keyed by normalized path; there is no backing real disk. Intended
// for scratch/temporary filesystems and as a mount target (see
// IFileSystemService::CreateComposedFileSystem).
class InMemoryFileSystem : public MountableFileSystem, public std::enable_shared_from_this<InMemoryFileSystem> {
public:
    static std::shared_ptr<InMemoryFileSystem> Create();
    ~InMemoryFileSystem() override;

    std::shared_ptr<IFileDescriptor> LocalOpenFile(const std::string& pathname, int flags) override;
    std::shared_ptr<IFileDescriptor> LocalOpenFile(const std::string& pathname, int flags, int mode) override;

    int LocalCreateDirectory(const std::string& pathname, int mode) override;
    int LocalRemoveDirectory(const std::string& pathname) override;
    int LocalRemoveFile(const std::string& pathname) override;
    std::vector<DirectoryEntry> LocalReadDirectory(const std::string& path) override;
    int LocalStat(const std::string& path, FileStatus& out) override;

    std::string AbsolutePathFor(const std::string& path) const override;

private:
    InMemoryFileSystem();

    friend class InMemoryFileDescriptor;

    struct Node {
        bool isDirectory = false;
        std::vector<char> data;
        // As st_atim, st_mtim and st_ctim. A directory's content is its list
        // of entries.
        FileDateTime accessTime;
        FileDateTime modificationTime;
        FileDateTime changeTime;

        static Node Make(bool isDirectory) {
            Node node;
            node.isDirectory = isDirectory;
            node.accessTime = node.modificationTime = node.changeTime = CurrentFileDateTime();
            return node;
        }
        // Its content changed just now.
        void Modified() { modificationTime = changeTime = CurrentFileDateTime(); }
    };

    // Marks a directory's entry list as changed now. Must be called with
    // m_mutex held.
    void TouchDirectory(const std::string& normalizedPath);

    // The read/write of a descriptor opened on |normalizedPath|, at its shared
    // |position|. WriteAt with |append| writes at the current end of the file
    // before every write, as O_APPEND does on POSIX. A node that is gone
    // (removed) gives kIOError. Both take m_mutex; the descriptor's position
    // is only ever touched inside them, under it.
    ssize_t ReadAt(const std::string& normalizedPath, size_t& position, void* buf, size_t count);
    ssize_t WriteAt(const std::string& normalizedPath, size_t& position, bool append, const void* buf, size_t count);

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, Node> m_nodes;
};

}
