#pragma once
#include "interfaces/IFileSystem.h"

namespace Haisos {

class FileSystem : public IFileSystem {
public:
    FileSystem() = default;
    ~FileSystem() override = default;

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
};

} // namespace Haisos
