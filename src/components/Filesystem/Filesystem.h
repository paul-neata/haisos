#pragma once
#include "MountableFileSystem.h"
#include "VirtualPath.h"
#include "interfaces/IFileSystemService.h"

namespace Haisos {

// An open file of the host's disk: a wrapper over the host's own descriptor,
// closing it when released. It is only ever made by FileSystem::LocalOpenFile.
class HostFileDescriptor final : public IFileDescriptor {
public:
    static std::shared_ptr<HostFileDescriptor> Create(int hostFd) {
        return std::shared_ptr<HostFileDescriptor>(new HostFileDescriptor(hostFd));
    }
    // Closes the host fd; logs a warning naming it if that fails.
    ~HostFileDescriptor() override;

    ssize_t Read(void* buf, size_t count) override;
    ssize_t Write(const void* buf, size_t count) override;
    // Always false: only console descriptors are terminals, whatever the host
    // fd is.
    bool IsTerminal() const override { return false; }

private:
    explicit HostFileDescriptor(int hostFd) : m_hostFd(hostFd) {}

    const int m_hostFd;
};

// The host's own file calls, on host paths, unrooted: whatever path it is
// handed is where it goes. Paths and names are UTF-8 on every platform; on
// Windows they reach the host through its UTF-16 ("W") calls, so any name
// works there, not only those of the ANSI code page. Every call runs with a
// NoCriticalErrorDialogs in scope.
class FileSystem : public MountableFileSystem {
public:
    static std::shared_ptr<FileSystem> Create() {
        return std::shared_ptr<FileSystem>(new FileSystem());
    }
    ~FileSystem() override = default;

    std::shared_ptr<IFileDescriptor> LocalOpenFile(const std::string& pathname, int flags) override;
    std::shared_ptr<IFileDescriptor> LocalOpenFile(const std::string& pathname, int flags, int mode) override;

    int LocalCreateDirectory(const std::string& pathname, int mode) override;
    int LocalRemoveDirectory(const std::string& pathname) override;
    int LocalRemoveFile(const std::string& pathname) override;
    std::vector<DirectoryEntry> LocalReadDirectory(const std::string& path) override;
    int LocalStat(const std::string& path, FileStatus& out) override;
    std::string AbsolutePathFor(const std::string& path) const override {
        return NormalizeVirtualPath(path, "/");
    }

    // Whether the host takes |hostPath| for a device rather than a file: on
    // Windows, one ending in a legacy device name (NUL, CON, COM1, ...), as
    // GetFullPathNameW says -- the very conversion CreateFileW makes. Which
    // names those are varies between Windows versions: Windows 10 takes
    // NUL.txt for the null device, Windows 11 for a file. Never elsewhere.
    static bool IsDevicePath(const std::string& hostPath);

private:
    FileSystem() = default;
};

} // namespace Haisos
