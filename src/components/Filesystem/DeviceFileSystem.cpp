#include "DeviceFileSystem.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include "FilesystemUtils.h"
#include "VirtualPath.h"

namespace Haisos {

const DeviceFileSystem::DeviceInfo DeviceFileSystem::kDevices[] = {
    {"null", Device::Null, 1, 3},
    {"zero", Device::Zero, 1, 5},
};

std::shared_ptr<DeviceFileSystem> DeviceFileSystem::Create() {
    return std::shared_ptr<DeviceFileSystem>(new DeviceFileSystem());
}

DeviceFileSystem::DeviceFileSystem() : m_createdTime(CurrentFileDateTime()) {}
DeviceFileSystem::~DeviceFileSystem() = default;

const DeviceFileSystem::DeviceInfo* DeviceFileSystem::DeviceAt(const std::string& normalizedPath) {
    for (const auto& info : kDevices) {
        if (normalizedPath == std::string("/") + info.name) {
            return &info;
        }
    }
    return nullptr;
}

namespace {

// An open device. Immutable, so it needs no mutex. The access mode it was
// opened with is enforced on every call: Write on a read-only descriptor and
// Read on a write-only one fail with kIOError, as EBADF would.
class DeviceFileDescriptor final : public IFileDescriptor {
public:
    static std::shared_ptr<DeviceFileDescriptor> Create(bool readsZeros, int flags) {
        return std::shared_ptr<DeviceFileDescriptor>(new DeviceFileDescriptor(readsZeros, flags));
    }

    ssize_t Read(void* buf, size_t count) override {
        if (!m_readable) {
            return kIOError;
        }
        if (!m_readsZeros) {
            return 0; // null: end of file at once
        }
        // As read() itself, never more than a ssize_t can report.
        const size_t filled = std::min(count, static_cast<size_t>(std::numeric_limits<ssize_t>::max()));
        std::memset(buf, 0, filled);
        return static_cast<ssize_t>(filled);
    }

    ssize_t Write(const void* /*buf*/, size_t count) override {
        if (!m_writable) {
            return kIOError;
        }
        // Both devices take everything, and keep none of it.
        return static_cast<ssize_t>(std::min(count, static_cast<size_t>(std::numeric_limits<ssize_t>::max())));
    }

    bool IsTerminal() const override { return false; }

private:
    DeviceFileDescriptor(bool readsZeros, int flags)
        : m_readsZeros(readsZeros)
        , m_writable((flags & (kFileWriteOnlyBit | kFileReadWriteBit)) != 0)
        , m_readable((flags & kFileWriteOnlyBit) == 0)
    {
    }

    const bool m_readsZeros;
    const bool m_writable;
    const bool m_readable;
};

} // namespace

std::shared_ptr<IFileDescriptor> DeviceFileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    return LocalOpenFile(pathname, flags, 0);
}

std::shared_ptr<IFileDescriptor> DeviceFileSystem::LocalOpenFile(const std::string& pathname, int flags, int /*mode*/) {
    // Only a device opens: the root is a directory, and anything else would
    // have to be created. A device takes any flags -- O_CREAT finds it already
    // there, and O_TRUNC has nothing to truncate.
    const DeviceInfo* info = DeviceAt(NormalizeVirtualPath(pathname));
    if (!info) {
        return nullptr;
    }
    return DeviceFileDescriptor::Create(info->device == Device::Zero, flags);
}

int DeviceFileSystem::LocalCreateDirectory(const std::string& /*pathname*/, int /*mode*/) {
    return -1;
}

int DeviceFileSystem::LocalRemoveDirectory(const std::string& /*pathname*/) {
    return -1;
}

int DeviceFileSystem::LocalRemoveFile(const std::string& /*pathname*/) {
    return -1;
}

int DeviceFileSystem::LocalRename(const std::string& /*oldPath*/, const std::string& /*newPath*/) {
    return kFileSystemError;
}

int DeviceFileSystem::LocalSetTimes(const std::string& path,
                                    const std::optional<FileDateTime>& /*accessTime*/,
                                    const std::optional<FileDateTime>& /*modificationTime*/) {
    // A device (or the root) keeps its times: the call succeeds, changing
    // nothing, as long as something is there to take it.
    FileStatus status;
    return LocalStat(path, status) == 0 ? 0 : kFileSystemError;
}

std::vector<DirectoryEntry> DeviceFileSystem::LocalReadDirectory(const std::string& path) {
    std::vector<DirectoryEntry> entries;
    if (NormalizeVirtualPath(path) != "/") {
        return entries;
    }
    for (const auto& info : kDevices) {
        entries.push_back(DirectoryEntry{info.name, DirectoryEntryType::CharDevice});
    }
    return entries;
}

int DeviceFileSystem::LocalStat(const std::string& path, FileStatus& out) {
    const std::string normalized = NormalizeVirtualPath(path);
    FileStatus status;
    status.accessTime = status.modificationTime = status.changeTime = m_createdTime;
    if (normalized == "/") {
        status.type = DirectoryEntryType::Dir;
        status.linkCount = 2; // its own "." and the entry naming it; no subdirectories
    } else if (const DeviceInfo* info = DeviceAt(normalized)) {
        status.type = DirectoryEntryType::CharDevice;
        status.deviceMajor = info->major;
        status.deviceMinor = info->minor;
    } else {
        return -1;
    }
    // A device stores nothing: size and blocks stay 0, as stat() reports them.
    out = status;
    return 0;
}

std::string DeviceFileSystem::AbsolutePathFor(const std::string& path) const {
    return NormalizeVirtualPath(path);
}

}
