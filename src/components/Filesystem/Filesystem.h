#pragma once
#include "MountableFileSystem.h"
#include "VirtualPath.h"
#include "interfaces/IFilesystemService.h"

namespace Haisos {

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
    int ChangeDirectory(const std::string& path) override;
    char* GetCurrentDirectory(std::string& buf, size_t size) override;

    std::vector<DirectoryEntry> LocalReadDirectory(const std::string& path) override;
    std::string AbsolutePathFor(const std::string& path) const override {
        return NormalizeVirtualPath(path, "/");
    }

private:
    FileSystem() = default;
};

} // namespace Haisos
