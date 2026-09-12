#pragma once
#include <memory>
#include <string>
#include "interfaces/IFilesystemService.h"

namespace Haisos {

// Wraps an existing IFileSystem, allowing only read/navigation operations.
// Every write, create, or remove is rejected; the wrapped filesystem is kept
// alive for as long as this view is.
class ReadOnlyFileSystem : public IFileSystem {
public:
    explicit ReadOnlyFileSystem(std::shared_ptr<IFileSystem> inner);
    ~ReadOnlyFileSystem() override;

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
    std::shared_ptr<IFileSystem> m_inner;
    // This view's own current directory; not delegated to inner's
    // ChangeDirectory (inner may be shared by other views/processes).
    std::string m_cwd = "/";
};

}
