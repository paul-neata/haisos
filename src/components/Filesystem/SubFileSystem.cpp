#include "SubFileSystem.h"
#include "VirtualPath.h"

namespace Haisos {

std::shared_ptr<SubFileSystem> SubFileSystem::Create(std::shared_ptr<IFileSystem> root, const std::string& basePath) {
    return std::shared_ptr<SubFileSystem>(new SubFileSystem(std::move(root), basePath));
}

SubFileSystem::SubFileSystem(std::shared_ptr<IFileSystem> root, const std::string& basePath)
    : m_root(std::move(root))
    , m_basePath(NormalizeVirtualPath(basePath))
{
}

SubFileSystem::~SubFileSystem() = default;

std::string SubFileSystem::ResolveInRoot(const std::string& path) const {
    std::string normalized = NormalizeVirtualPath(path);
    if (m_basePath == "/") {
        return normalized;
    }
    return (normalized == "/") ? m_basePath : (m_basePath + normalized);
}

int SubFileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    return m_root->OpenFile(ResolveInRoot(pathname), flags);
}

int SubFileSystem::LocalOpenFile(const std::string& pathname, int flags, int mode) {
    return m_root->OpenFile(ResolveInRoot(pathname), flags, mode);
}

int SubFileSystem::LocalCloseFile(int fd) {
    return m_root->CloseFile(fd);
}

ssize_t SubFileSystem::LocalReadFile(int fd, void* buf, size_t count) {
    return m_root->ReadFile(fd, buf, count);
}

ssize_t SubFileSystem::LocalWriteFile(int fd, const void* buf, size_t count) {
    return m_root->WriteFile(fd, buf, count);
}

int SubFileSystem::LocalCreateDirectory(const std::string& pathname, int mode) {
    return m_root->CreateDirectory(ResolveInRoot(pathname), mode);
}

int SubFileSystem::LocalRemoveDirectory(const std::string& pathname) {
    return m_root->RemoveDirectory(ResolveInRoot(pathname));
}

int SubFileSystem::LocalRemoveFile(const std::string& pathname) {
    return m_root->RemoveFile(ResolveInRoot(pathname));
}

std::vector<DirectoryEntry> SubFileSystem::LocalReadDirectory(const std::string& path) {
    return m_root->ReadDirectory(ResolveInRoot(path));
}

int SubFileSystem::LocalStat(const std::string& path, FileStatus& out) {
    return m_root->Stat(ResolveInRoot(path), out);
}

std::optional<std::string> SubFileSystem::LocalIsBuiltinCommand(const std::string& path) {
    return m_root->IsBuiltinCommand(ResolveInRoot(path));
}

std::string SubFileSystem::AbsolutePathFor(const std::string& path) const {
    return NormalizeVirtualPath(path);
}

}
