#include "MountPoints.h"
#include <algorithm>
#include "VirtualPath.h"

namespace Haisos {

namespace {

// "/a/b" contains "/a/b" and "/a/b/c", but not "/a/bc".
bool IsAtOrUnder(const std::string& path, const std::string& mountPoint) {
    if (mountPoint == "/") {
        return true;
    }
    if (path.compare(0, mountPoint.size(), mountPoint) != 0) {
        return false;
    }
    return path.size() == mountPoint.size() || path[mountPoint.size()] == '/';
}

} // namespace

void MountPoints::Mount(const std::string& path, std::shared_ptr<IFileSystem> filesystem) {
    if (!filesystem) {
        return;
    }
    const std::string normalized = NormalizeVirtualPath(path, "/");
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& entry : m_mounts) {
        if (entry.path == normalized) {
            // Mounting over an existing mount point replaces it, the way a
            // second mount on the same directory shadows the first.
            entry.filesystem = std::move(filesystem);
            return;
        }
    }
    m_mounts.push_back(Entry{normalized, std::move(filesystem)});
    // Longest first, so Resolve() finds the innermost mount for nested mounts.
    std::sort(m_mounts.begin(), m_mounts.end(),
        [](const Entry& a, const Entry& b) { return a.path.size() > b.path.size(); });
}

void MountPoints::Unmount(const std::string& path) {
    const std::string normalized = NormalizeVirtualPath(path, "/");
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mounts.erase(
        std::remove_if(m_mounts.begin(), m_mounts.end(),
            [&normalized](const Entry& entry) { return entry.path == normalized; }),
        m_mounts.end());
}

bool MountPoints::Empty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mounts.empty();
}

MountPoints::Route MountPoints::Resolve(const std::string& path) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& entry : m_mounts) {
        if (!IsAtOrUnder(path, entry.path)) {
            continue;
        }
        Route route;
        route.filesystem = entry.filesystem.get();
        auto relative = RelativeToBase(entry.path, path);
        route.innerPath = relative ? *relative : "/";
        return route;
    }
    return Route{};
}

int MountPoints::RegisterFd(IFileSystem* filesystem, int innerFd) {
    if (innerFd < 0) {
        return innerFd;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const int fd = m_nextFd++;
    m_openFds[fd] = Handle{filesystem, innerFd};
    return fd;
}

bool MountPoints::LookupFd(int fd, IFileSystem*& filesystem, int& innerFd) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_openFds.find(fd);
    if (it == m_openFds.end()) {
        return false;
    }
    filesystem = it->second.filesystem;
    innerFd = it->second.innerFd;
    return true;
}

void MountPoints::ReleaseFd(int fd) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_openFds.erase(fd);
}

std::vector<std::string> MountPoints::ChildSegments(const std::string& directory) const {
    const std::string base = (directory == "/") ? "" : directory;
    std::vector<std::string> segments;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& entry : m_mounts) {
        if (entry.path.size() <= base.size() || !IsAtOrUnder(entry.path, base.empty() ? "/" : base)) {
            continue;
        }
        std::string remainder = entry.path.substr(base.size() + 1);
        auto slash = remainder.find('/');
        std::string segment = (slash == std::string::npos) ? remainder : remainder.substr(0, slash);
        if (!segment.empty() &&
            std::find(segments.begin(), segments.end(), segment) == segments.end()) {
            segments.push_back(segment);
        }
    }
    return segments;
}

}
