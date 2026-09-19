#pragma once
#include <string>
#include "MountableFileSystem.h"
#include "interfaces/IFilesystemService.h"
#include "Filesystem.h"

namespace Haisos {

// A FileSystem jailed to a real disk directory: every path passed in is
// resolved and validated to stay within rootPath before being forwarded to the
// real filesystem (a request that would escape rootPath via ".." fails).
//
// There is not yet a virtual/independent current directory for the jail:
// ChangeDirectory still changes the real process's current directory (after
// validating the target stays within rootPath), matching how the rest of
// IFileSystem is a thin wrapper over real OS state. This is deliberately
// scoped for now -- composed/temporary/copy-on-write filesystems are a
// documented future extension (see IHaisosOS).
class PhysicalFileSystem : public MountableFileSystem {
public:
    static std::shared_ptr<PhysicalFileSystem> Create(const std::string& rootPath);
    ~PhysicalFileSystem() override = default;

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

private:
    explicit PhysicalFileSystem(const std::string& rootPath);

    // Resolves pathname against the root and validates it does not escape it.
    // Returns false (leaving resolved untouched) if the path is invalid.
    bool ResolveWithinRoot(const std::string& pathname, std::string& resolved) const;

    std::string m_rootPath;
    std::shared_ptr<FileSystem> m_inner;
};

} // namespace Haisos
