#pragma once
#include <memory>
#include <string>
#include "MountableFileSystem.h"
#include "interfaces/IFileSystemService.h"

namespace Haisos {

// Wraps an existing IFileSystem, allowing only read/navigation operations.
// Every write, create, or remove is rejected; the wrapped filesystem is kept
// alive for as long as this filesystem is.
class ReadOnlyFileSystem : public MountableFileSystem {
public:
    static std::shared_ptr<ReadOnlyFileSystem> Create(std::shared_ptr<IFileSystem> inner);
    ~ReadOnlyFileSystem() override;

    std::shared_ptr<IFileDescriptor> LocalOpenFile(const std::string& pathname, int flags) override;
    std::shared_ptr<IFileDescriptor> LocalOpenFile(const std::string& pathname, int flags, int mode) override;

    int LocalCreateDirectory(const std::string& pathname, int mode) override;
    int LocalRemoveDirectory(const std::string& pathname) override;
    int LocalRemoveFile(const std::string& pathname) override;
    int LocalRename(const std::string& oldPath, const std::string& newPath) override;
    int LocalSetTimes(const std::string& path,
                      const std::optional<FileDateTime>& accessTime,
                      const std::optional<FileDateTime>& modificationTime) override;

    std::vector<DirectoryEntry> LocalReadDirectory(const std::string& path) override;
    int LocalStat(const std::string& path, FileStatus& out) override;
    std::optional<std::string> LocalIsBuiltinCommand(const std::string& path) override;

    std::string AbsolutePathFor(const std::string& path) const override;

private:
    explicit ReadOnlyFileSystem(std::shared_ptr<IFileSystem> inner);

    std::shared_ptr<IFileSystem> m_inner;
};

}
