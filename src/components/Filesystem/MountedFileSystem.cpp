#include "MountedFileSystem.h"
#include "VirtualPath.h"

namespace Haisos {

namespace {

// If ancestorPath is a strict ancestor of mountPoint, returns the single next
// path segment on the way from ancestorPath towards mountPoint.
std::optional<std::string> NextMountSegment(const std::string& ancestorPath, const std::string& mountPoint) {
    auto rel = RelativeToBase(ancestorPath, mountPoint);
    if (!rel.has_value() || *rel == "/") {
        return std::nullopt;
    }
    const std::string& r = *rel; // starts with '/', e.g. "/data" or "/data/sub"
    auto pos = r.find('/', 1);
    return (pos == std::string::npos) ? r.substr(1) : r.substr(1, pos - 1);
}

}

MountedFileSystem::MountedFileSystem(std::shared_ptr<IFileSystem> main, const std::string& whereToMount, std::shared_ptr<IFileSystem> mounted)
    : m_main(std::move(main))
    , m_mountPoint(NormalizeVirtualPath(whereToMount))
    , m_mounted(std::move(mounted))
{
}

MountedFileSystem::~MountedFileSystem() = default;

std::string MountedFileSystem::GetCwd() const {
    std::lock_guard<std::mutex> lock(m_cwdMutex);
    return m_cwd;
}

int MountedFileSystem::OpenFile(const std::string& pathname, int flags) {
    std::string normalized = NormalizeVirtualPath(pathname, GetCwd());
    if (auto rel = RelativeToBase(m_mountPoint, normalized)) {
        int fd = m_mounted->OpenFile(*rel, flags);
        if (fd >= 0) {
            std::lock_guard<std::mutex> lock(m_openFdsMutex);
            m_mountedFds.insert(fd);
        }
        return fd;
    }
    return m_main->OpenFile(normalized, flags);
}

int MountedFileSystem::OpenFile(const std::string& pathname, int flags, int mode) {
    std::string normalized = NormalizeVirtualPath(pathname, GetCwd());
    if (auto rel = RelativeToBase(m_mountPoint, normalized)) {
        int fd = m_mounted->OpenFile(*rel, flags, mode);
        if (fd >= 0) {
            std::lock_guard<std::mutex> lock(m_openFdsMutex);
            m_mountedFds.insert(fd);
        }
        return fd;
    }
    return m_main->OpenFile(normalized, flags, mode);
}

int MountedFileSystem::CloseFile(int fd) {
    std::lock_guard<std::mutex> lock(m_openFdsMutex);
    if (m_mountedFds.erase(fd) > 0) {
        return m_mounted->CloseFile(fd);
    }
    return m_main->CloseFile(fd);
}

ssize_t MountedFileSystem::ReadFile(int fd, void* buf, size_t count) {
    bool isMounted;
    {
        std::lock_guard<std::mutex> lock(m_openFdsMutex);
        isMounted = m_mountedFds.count(fd) > 0;
    }
    return isMounted ? m_mounted->ReadFile(fd, buf, count) : m_main->ReadFile(fd, buf, count);
}

ssize_t MountedFileSystem::WriteFile(int fd, const void* buf, size_t count) {
    bool isMounted;
    {
        std::lock_guard<std::mutex> lock(m_openFdsMutex);
        isMounted = m_mountedFds.count(fd) > 0;
    }
    return isMounted ? m_mounted->WriteFile(fd, buf, count) : m_main->WriteFile(fd, buf, count);
}

int MountedFileSystem::CreateDirectory(const std::string& pathname, int mode) {
    std::string normalized = NormalizeVirtualPath(pathname, GetCwd());
    if (auto rel = RelativeToBase(m_mountPoint, normalized)) {
        return m_mounted->CreateDirectory(*rel, mode);
    }
    return m_main->CreateDirectory(normalized, mode);
}

int MountedFileSystem::RemoveDirectory(const std::string& pathname) {
    std::string normalized = NormalizeVirtualPath(pathname, GetCwd());
    if (auto rel = RelativeToBase(m_mountPoint, normalized)) {
        return m_mounted->RemoveDirectory(*rel);
    }
    return m_main->RemoveDirectory(normalized);
}

int MountedFileSystem::ChangeDirectory(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_cwdMutex);
    m_cwd = NormalizeVirtualPath(path, m_cwd);
    return 0;
}

char* MountedFileSystem::GetCurrentDirectory(std::string& buf, size_t size) {
    std::lock_guard<std::mutex> lock(m_cwdMutex);
    if (m_cwd.size() + 1 > size) {
        return nullptr;
    }
    buf = m_cwd;
    return &buf[0];
}

std::vector<DirectoryEntry> MountedFileSystem::ReadDirectory(const std::string& path) {
    std::string normalized = NormalizeVirtualPath(path, GetCwd());

    if (auto rel = RelativeToBase(m_mountPoint, normalized)) {
        return m_mounted->ReadDirectory(*rel);
    }

    std::vector<DirectoryEntry> entries = m_main->ReadDirectory(normalized);

    if (auto nextSegment = NextMountSegment(normalized, m_mountPoint)) {
        bool alreadyListed = false;
        for (const auto& entry : entries) {
            if (entry.name == *nextSegment) {
                alreadyListed = true;
                break;
            }
        }
        if (!alreadyListed) {
            entries.push_back(DirectoryEntry{*nextSegment, DirectoryEntryType::Dir});
        }
    }

    return entries;
}

}
