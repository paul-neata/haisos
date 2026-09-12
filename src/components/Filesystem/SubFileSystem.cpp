#include "SubFileSystem.h"
#include "VirtualPath.h"

namespace Haisos {

SubFileSystem::SubFileSystem(std::shared_ptr<IFileSystem> root, const std::string& basePath)
    : m_root(std::move(root))
    , m_basePath(NormalizeVirtualPath(basePath))
{
}

SubFileSystem::~SubFileSystem() = default;

std::string SubFileSystem::ResolveInRoot(const std::string& path) const {
    std::string cwd;
    {
        std::lock_guard<std::mutex> lock(m_cwdMutex);
        cwd = m_cwd;
    }
    std::string normalized = NormalizeVirtualPath(path, cwd);
    if (m_basePath == "/") {
        return normalized;
    }
    return (normalized == "/") ? m_basePath : (m_basePath + normalized);
}

int SubFileSystem::OpenFile(const std::string& pathname, int flags) {
    return m_root->OpenFile(ResolveInRoot(pathname), flags);
}

int SubFileSystem::OpenFile(const std::string& pathname, int flags, int mode) {
    return m_root->OpenFile(ResolveInRoot(pathname), flags, mode);
}

int SubFileSystem::CloseFile(int fd) {
    return m_root->CloseFile(fd);
}

ssize_t SubFileSystem::ReadFile(int fd, void* buf, size_t count) {
    return m_root->ReadFile(fd, buf, count);
}

ssize_t SubFileSystem::WriteFile(int fd, const void* buf, size_t count) {
    return m_root->WriteFile(fd, buf, count);
}

int SubFileSystem::CreateDirectory(const std::string& pathname, int mode) {
    return m_root->CreateDirectory(ResolveInRoot(pathname), mode);
}

int SubFileSystem::RemoveDirectory(const std::string& pathname) {
    return m_root->RemoveDirectory(ResolveInRoot(pathname));
}

int SubFileSystem::ChangeDirectory(const std::string& path) {
    // Kept purely virtual (see header): root may be shared by other views.
    std::lock_guard<std::mutex> lock(m_cwdMutex);
    m_cwd = NormalizeVirtualPath(path, m_cwd);
    return 0;
}

char* SubFileSystem::GetCurrentDirectory(std::string& buf, size_t size) {
    std::lock_guard<std::mutex> lock(m_cwdMutex);
    if (m_cwd.size() + 1 > size) {
        return nullptr;
    }
    buf = m_cwd;
    return &buf[0];
}

std::vector<DirectoryEntry> SubFileSystem::ReadDirectory(const std::string& path) {
    return m_root->ReadDirectory(ResolveInRoot(path));
}

}
