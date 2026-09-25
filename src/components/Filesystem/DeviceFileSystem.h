#pragma once
#include <mutex>
#include <string>
#include <unordered_map>
#include "MountableFileSystem.h"
#include "interfaces/IFileSystemService.h"

namespace Haisos {

// A device filesystem, as Linux's /dev: a root directory holding a fixed set
// of character devices, each behaving as its Linux namesake. Nothing can be
// created in it or removed from it -- no file, directory or builtin command --
// though a device can be opened for writing, with or without O_CREAT/O_TRUNC,
// as a shell's "> /dev/null" does.
//   null  writes are discarded; reads return end-of-file at once
//   zero  writes are discarded; reads return as many 0 bytes as asked for
// Intended to be mounted at /dev (see IFileSystemService::CreateDeviceFileSystem).
class DeviceFileSystem : public MountableFileSystem {
public:
    static std::shared_ptr<DeviceFileSystem> Create();
    ~DeviceFileSystem() override;

    int LocalOpenFile(const std::string& pathname, int flags) override;
    int LocalOpenFile(const std::string& pathname, int flags, int mode) override;
    int LocalCloseFile(int fd) override;
    ssize_t LocalReadFile(int fd, void* buf, size_t count) override;
    ssize_t LocalWriteFile(int fd, const void* buf, size_t count) override;

    int LocalCreateDirectory(const std::string& pathname, int mode) override;
    int LocalRemoveDirectory(const std::string& pathname) override;
    int LocalRemoveFile(const std::string& pathname) override;
    std::vector<DirectoryEntry> LocalReadDirectory(const std::string& path) override;
    int LocalStat(const std::string& path, FileStatus& out) override;
    bool LocalCanHoldBuiltinCommands() const override { return false; }

    std::string AbsolutePathFor(const std::string& path) const override;

private:
    DeviceFileSystem();

    enum class Device { Null, Zero };

    struct DeviceInfo {
        const char* name;
        Device device;
        // Linux's device numbers for it, which Stat reports.
        uint32_t major;
        uint32_t minor;
    };
    static const DeviceInfo kDevices[];

    // The device at |normalizedPath| ("/null"), or nullptr.
    static const DeviceInfo* DeviceAt(const std::string& normalizedPath);

    // When this filesystem was made: the time of its root and of every device,
    // as Linux's /dev shows the time it was populated at boot.
    const FileDateTime m_createdTime;

    std::mutex m_mutex;
    std::unordered_map<int, Device> m_openHandles;
    int m_nextFd = 3;
};

}
