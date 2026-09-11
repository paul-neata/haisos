#pragma once
#include <string>
#include "interfaces/IFilesystemService.h"
#include "Filesystem.h"

namespace Haisos {

// A FileSystem view jailed to a real disk directory: every path passed in is
// resolved and validated to stay within rootPath before being forwarded to the
// real filesystem (a request that would escape rootPath via ".." fails).
//
// There is not yet a virtual/independent current directory for the jail:
// ChangeDirectory still changes the real process's current directory (after
// validating the target stays within rootPath), matching how the rest of
// IFileSystem is a thin wrapper over real OS state. This is deliberately
// scoped for now -- composed/temporary/copy-on-write filesystems are a
// documented future extension (see IHaisosOS).
class PhysicalFileSystem : public IFileSystem {
public:
    explicit PhysicalFileSystem(const std::string& rootPath);
    ~PhysicalFileSystem() override = default;

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
    // Resolves pathname against the root and validates it does not escape it.
    // Returns false (leaving resolved untouched) if the path is invalid.
    bool ResolveWithinRoot(const std::string& pathname, std::string& resolved) const;

    std::string m_rootPath;
    FileSystem m_inner;
};

} // namespace Haisos
