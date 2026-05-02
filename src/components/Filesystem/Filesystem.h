#pragma once
#include "interfaces/IFilesystem.h"

namespace Haisos {

class Filesystem : public IFilesystem {
public:
    Filesystem() = default;
    ~Filesystem() override = default;

    int OpenFile(const std::string& pathname, int flags) override;
    int OpenFile(const std::string& pathname, int flags, int mode) override;
    int CloseFile(int fd) override;
    int ReadFile(int fd, void* buf, size_t count) override;
    int WriteFile(int fd, const void* buf, size_t count) override;

    int CreateDirectory(const std::string& pathname, int mode) override;
    int RemoveDirectory(const std::string& pathname) override;
    int ChangeDirectory(const std::string& path) override;
    char* GetCurrentDirectory(std::string& buf, size_t size) override;

    std::vector<DirectoryEntry> ReadDirectory(const std::string& path) override;
};

} // namespace Haisos
