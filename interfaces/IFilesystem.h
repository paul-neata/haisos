#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstddef>
#include <sys/types.h>

namespace Haisos {

enum DirectoryEntryType : char {
    File = 'f',
    Dir = 'd'
};

struct DirectoryEntry {
    std::string name;
    char type;
};

class IFilesystem {
public:
    virtual ~IFilesystem() = default;

    virtual int OpenFile(const std::string& pathname, int flags) = 0;
    virtual int OpenFile(const std::string& pathname, int flags, mode_t mode) = 0;
    virtual int CloseFile(int fd) = 0;
    virtual ssize_t ReadFile(int fd, void* buf, size_t count) = 0;
    virtual ssize_t WriteFile(int fd, const void* buf, size_t count) = 0;

    virtual int CreateDirectory(const std::string& pathname, mode_t mode) = 0;
    virtual int RemoveDirectory(const std::string& pathname) = 0;
    virtual int ChangeDirectory(const std::string& path) = 0;
    virtual char* GetCurrentDirectory(std::string& buf, size_t size) = 0;

    virtual std::vector<DirectoryEntry> ReadDirectory(const std::string& path) = 0;
};

std::unique_ptr<IFilesystem> CreateFilesystem();

} // namespace Haisos
