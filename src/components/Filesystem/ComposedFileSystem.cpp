#include "ComposedFileSystem.h"
#include <limits>
#include "VirtualPath.h"
#include "src/components/Logger/Logger.h"

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

ComposedFileSystem::ComposedFileSystem(std::shared_ptr<IFileSystem> main, const std::string& whereToMount, std::shared_ptr<IFileSystem> mounted)
    : m_main(std::move(main))
    , m_mountPoint(NormalizeVirtualPath(whereToMount))
    , m_mounted(std::move(mounted))
{
}

ComposedFileSystem::~ComposedFileSystem() = default;

std::string ComposedFileSystem::GetCwd() const {
    std::lock_guard<std::mutex> lock(m_cwdMutex);
    return m_cwd;
}

const std::shared_ptr<IFileSystem>& ComposedFileSystem::FileSystemFor(Side side) const {
    return (side == Side::Mounted) ? m_mounted : m_main;
}

int ComposedFileSystem::AllocateFdLocked() {
    // At most one more candidate than there are live fds has to be tried before
    // an unused one turns up (the candidates are distinct until the counter
    // wraps). Wrapping, rather than incrementing past INT_MAX, also keeps the
    // counter out of signed-overflow territory.
    for (size_t attempts = 0; attempts <= m_openFds.size(); ++attempts) {
        int fd = m_nextFd;
        m_nextFd = (fd == std::numeric_limits<int>::max()) ? kFirstSyntheticFd : (fd + 1);
        if (m_openFds.find(fd) == m_openFds.end()) {
            return fd;
        }
    }
    return -1;
}

int ComposedFileSystem::RegisterFd(Side side, int innerFd) {
    if (innerFd < 0) {
        return innerFd; // pass the underlying filesystem's error value through
    }

    int fd;
    {
        std::lock_guard<std::mutex> lock(m_openFdsMutex);
        fd = AllocateFdLocked();
        if (fd >= 0) {
            m_openFds.emplace(fd, Handle{side, innerFd});
        }
    }

    if (fd < 0) {
        LogError("ComposedFileSystem: no free file descriptor left (mount point: %s)", m_mountPoint.c_str());
        FileSystemFor(side)->CloseFile(innerFd); // don't leak the underlying handle
        return -1;
    }
    return fd;
}

bool ComposedFileSystem::LookupFd(int fd, Handle& handle) const {
    std::lock_guard<std::mutex> lock(m_openFdsMutex);
    auto it = m_openFds.find(fd);
    if (it == m_openFds.end()) {
        return false;
    }
    handle = it->second;
    return true;
}

int ComposedFileSystem::OpenFile(const std::string& pathname, int flags) {
    std::string normalized = NormalizeVirtualPath(pathname, GetCwd());
    if (auto rel = RelativeToBase(m_mountPoint, normalized)) {
        return RegisterFd(Side::Mounted, m_mounted->OpenFile(*rel, flags));
    }
    return RegisterFd(Side::Main, m_main->OpenFile(normalized, flags));
}

int ComposedFileSystem::OpenFile(const std::string& pathname, int flags, int mode) {
    std::string normalized = NormalizeVirtualPath(pathname, GetCwd());
    if (auto rel = RelativeToBase(m_mountPoint, normalized)) {
        return RegisterFd(Side::Mounted, m_mounted->OpenFile(*rel, flags, mode));
    }
    return RegisterFd(Side::Main, m_main->OpenFile(normalized, flags, mode));
}

int ComposedFileSystem::CloseFile(int fd) {
    Handle handle;
    {
        std::lock_guard<std::mutex> lock(m_openFdsMutex);
        auto it = m_openFds.find(fd);
        if (it == m_openFds.end()) {
            return -1;
        }
        handle = it->second;
        m_openFds.erase(it);
    }
    return FileSystemFor(handle.side)->CloseFile(handle.innerFd);
}

ssize_t ComposedFileSystem::ReadFile(int fd, void* buf, size_t count) {
    Handle handle;
    if (!LookupFd(fd, handle)) {
        return -1;
    }
    return FileSystemFor(handle.side)->ReadFile(handle.innerFd, buf, count);
}

ssize_t ComposedFileSystem::WriteFile(int fd, const void* buf, size_t count) {
    Handle handle;
    if (!LookupFd(fd, handle)) {
        return -1;
    }
    return FileSystemFor(handle.side)->WriteFile(handle.innerFd, buf, count);
}

int ComposedFileSystem::CreateDirectory(const std::string& pathname, int mode) {
    std::string normalized = NormalizeVirtualPath(pathname, GetCwd());
    if (auto rel = RelativeToBase(m_mountPoint, normalized)) {
        return m_mounted->CreateDirectory(*rel, mode);
    }
    return m_main->CreateDirectory(normalized, mode);
}

int ComposedFileSystem::RemoveDirectory(const std::string& pathname) {
    std::string normalized = NormalizeVirtualPath(pathname, GetCwd());
    if (auto rel = RelativeToBase(m_mountPoint, normalized)) {
        return m_mounted->RemoveDirectory(*rel);
    }
    return m_main->RemoveDirectory(normalized);
}

int ComposedFileSystem::ChangeDirectory(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_cwdMutex);
    m_cwd = NormalizeVirtualPath(path, m_cwd);
    return 0;
}

char* ComposedFileSystem::GetCurrentDirectory(std::string& buf, size_t size) {
    std::lock_guard<std::mutex> lock(m_cwdMutex);
    if (m_cwd.size() + 1 > size) {
        return nullptr;
    }
    buf = m_cwd;
    return &buf[0];
}

std::vector<DirectoryEntry> ComposedFileSystem::ReadDirectory(const std::string& path) {
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
