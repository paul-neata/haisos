#include "ReadOnlyFileSystem.h"
#include "FilesystemUtils.h"

namespace Haisos {

ReadOnlyFileSystem::ReadOnlyFileSystem(std::shared_ptr<IFileSystem> inner) : m_inner(std::move(inner)) {}
ReadOnlyFileSystem::~ReadOnlyFileSystem() = default;

int ReadOnlyFileSystem::OpenFile(const std::string& pathname, int flags) {
    if (RequestsWriteAccess(flags)) {
        return -1;
    }
    return m_inner->OpenFile(pathname, flags);
}

int ReadOnlyFileSystem::OpenFile(const std::string& pathname, int flags, int mode) {
    if (RequestsWriteAccess(flags)) {
        return -1;
    }
    return m_inner->OpenFile(pathname, flags, mode);
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
    return m_inner->ChangeDirectory(path);
}

char* ReadOnlyFileSystem::GetCurrentDirectory(std::string& buf, size_t size) {
    return m_inner->GetCurrentDirectory(buf, size);
}

std::vector<DirectoryEntry> ReadOnlyFileSystem::ReadDirectory(const std::string& path) {
    return m_inner->ReadDirectory(path);
}

}
