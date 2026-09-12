#include "ReadOnlyFileSystem.h"
#include "FilesystemUtils.h"
#include "VirtualPath.h"

namespace Haisos {

ReadOnlyFileSystem::ReadOnlyFileSystem(std::shared_ptr<IFileSystem> inner) : m_inner(std::move(inner)) {}
ReadOnlyFileSystem::~ReadOnlyFileSystem() = default;

std::string ReadOnlyFileSystem::GetCwd() const {
    std::lock_guard<std::mutex> lock(m_cwdMutex);
    return m_cwd;
}

int ReadOnlyFileSystem::OpenFile(const std::string& pathname, int flags) {
    if (RequestsWriteAccess(flags)) {
        return -1;
    }
    return m_inner->OpenFile(NormalizeVirtualPath(pathname, GetCwd()), flags);
}

int ReadOnlyFileSystem::OpenFile(const std::string& pathname, int flags, int mode) {
    if (RequestsWriteAccess(flags)) {
        return -1;
    }
    return m_inner->OpenFile(NormalizeVirtualPath(pathname, GetCwd()), flags, mode);
}

int ReadOnlyFileSystem::CloseFile(int fd) {
    return m_inner->CloseFile(fd);
}

ssize_t ReadOnlyFileSystem::ReadFile(int fd, void* buf, size_t count) {
    return m_inner->ReadFile(fd, buf, count);
}

ssize_t ReadOnlyFileSystem::WriteFile(int /*fd*/, const void* /*buf*/, size_t /*count*/) {
    return -1;
}

int ReadOnlyFileSystem::CreateDirectory(const std::string& /*pathname*/, int /*mode*/) {
    return -1;
}

int ReadOnlyFileSystem::RemoveDirectory(const std::string& /*pathname*/) {
    return -1;
}

int ReadOnlyFileSystem::ChangeDirectory(const std::string& path) {
    // Kept purely virtual (see header): inner may be shared by other views.
    std::lock_guard<std::mutex> lock(m_cwdMutex);
    m_cwd = NormalizeVirtualPath(path, m_cwd);
    return 0;
}

char* ReadOnlyFileSystem::GetCurrentDirectory(std::string& buf, size_t size) {
    std::lock_guard<std::mutex> lock(m_cwdMutex);
    if (m_cwd.size() + 1 > size) {
        return nullptr;
    }
    buf = m_cwd;
    return &buf[0];
}

std::vector<DirectoryEntry> ReadOnlyFileSystem::ReadDirectory(const std::string& path) {
    return m_inner->ReadDirectory(NormalizeVirtualPath(path, GetCwd()));
}

}
