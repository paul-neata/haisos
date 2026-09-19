#include "ReadOnlyFileSystem.h"
#include "FilesystemUtils.h"
#include "VirtualPath.h"

namespace Haisos {

std::shared_ptr<ReadOnlyFileSystem> ReadOnlyFileSystem::Create(std::shared_ptr<IFileSystem> inner) {
    return std::shared_ptr<ReadOnlyFileSystem>(new ReadOnlyFileSystem(std::move(inner)));
}

ReadOnlyFileSystem::ReadOnlyFileSystem(std::shared_ptr<IFileSystem> inner) : m_inner(std::move(inner)) {}
ReadOnlyFileSystem::~ReadOnlyFileSystem() = default;

std::string ReadOnlyFileSystem::GetCwd() const {
    std::lock_guard<std::mutex> lock(m_cwdMutex);
    return m_cwd;
}

int ReadOnlyFileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    if (RequestsWriteAccess(flags)) {
        return -1;
    }
    return m_inner->OpenFile(NormalizeVirtualPath(pathname, GetCwd()), flags);
}

int ReadOnlyFileSystem::LocalOpenFile(const std::string& pathname, int flags, int mode) {
    if (RequestsWriteAccess(flags)) {
        return -1;
    }
    return m_inner->OpenFile(NormalizeVirtualPath(pathname, GetCwd()), flags, mode);
}

int ReadOnlyFileSystem::LocalCloseFile(int fd) {
    return m_inner->CloseFile(fd);
}

ssize_t ReadOnlyFileSystem::LocalReadFile(int fd, void* buf, size_t count) {
    return m_inner->ReadFile(fd, buf, count);
}

ssize_t ReadOnlyFileSystem::LocalWriteFile(int /*fd*/, const void* /*buf*/, size_t /*count*/) {
    return -1;
}

int ReadOnlyFileSystem::LocalCreateDirectory(const std::string& /*pathname*/, int /*mode*/) {
    return -1;
}

int ReadOnlyFileSystem::LocalRemoveDirectory(const std::string& /*pathname*/) {
    return -1;
}

int ReadOnlyFileSystem::ChangeDirectory(const std::string& path) {
    // Kept purely virtual (see header): inner may be shared by other composed filesystems.
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

std::vector<DirectoryEntry> ReadOnlyFileSystem::LocalReadDirectory(const std::string& path) {
    return m_inner->ReadDirectory(NormalizeVirtualPath(path, GetCwd()));
}


std::string ReadOnlyFileSystem::AbsolutePathFor(const std::string& path) const {
    return NormalizeVirtualPath(path, GetCwd());
}

}
