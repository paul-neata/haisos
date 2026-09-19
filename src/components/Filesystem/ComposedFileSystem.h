#pragma once
#include <memory>
#include <string>
#include "MountableFileSystem.h"
#include "interfaces/IFileSystemService.h"

namespace Haisos {

// A composition of |main| with |mounted| overlaid at |whereToMount|: paths at or
// under whereToMount are served by mounted, overriding anything main has there;
// every other path is served by main, unchanged. Neither operand is mutated --
// this is a new composed filesystem, not an in-place operation. Use
// IFileSystem::Mount when you do want to change a filesystem in place.
//
// The overlay is just a mount point on this filesystem, so all the routing,
// file-descriptor translation and mount-point listing come from
// MountableFileSystem; what is left here is a filesystem that otherwise
// delegates to main.
class ComposedFileSystem : public MountableFileSystem {
public:
    static std::shared_ptr<ComposedFileSystem> Create(std::shared_ptr<IFileSystem> main, const std::string& whereToMount, std::shared_ptr<IFileSystem> mounted);
    ~ComposedFileSystem() override;

protected:
    std::string AbsolutePathFor(const std::string& path) const override;

    int LocalOpenFile(const std::string& pathname, int flags) override;
    int LocalOpenFile(const std::string& pathname, int flags, int mode) override;
    int LocalCloseFile(int fd) override;
    ssize_t LocalReadFile(int fd, void* buf, size_t count) override;
    ssize_t LocalWriteFile(int fd, const void* buf, size_t count) override;
    int LocalCreateDirectory(const std::string& pathname, int mode) override;
    int LocalRemoveDirectory(const std::string& pathname) override;
    std::vector<DirectoryEntry> LocalReadDirectory(const std::string& path) override;

private:
    ComposedFileSystem(std::shared_ptr<IFileSystem> main, const std::string& whereToMount, std::shared_ptr<IFileSystem> mounted);

    std::shared_ptr<IFileSystem> m_main;
};

}
