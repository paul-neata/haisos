#include "ReadOnlyFileSystem.h"
#include "FilesystemUtils.h"
#include "VirtualPath.h"

namespace Haisos {

std::shared_ptr<ReadOnlyFileSystem> ReadOnlyFileSystem::Create(std::shared_ptr<IFileSystem> inner) {
    return std::shared_ptr<ReadOnlyFileSystem>(new ReadOnlyFileSystem(std::move(inner)));
}

ReadOnlyFileSystem::ReadOnlyFileSystem(std::shared_ptr<IFileSystem> inner) : m_inner(std::move(inner)) {}
ReadOnlyFileSystem::~ReadOnlyFileSystem() = default;

std::shared_ptr<IFileDescriptor> ReadOnlyFileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    if (RequestsWriteAccess(flags)) {
        return nullptr;
    }
    // The inner filesystem's descriptor is passed up untouched: it was opened
    // read-only, so it refuses a write by itself.
    return m_inner->OpenFile(NormalizeVirtualPath(pathname), flags);
}

std::shared_ptr<IFileDescriptor> ReadOnlyFileSystem::LocalOpenFile(const std::string& pathname, int flags, int mode) {
    if (RequestsWriteAccess(flags)) {
        return nullptr;
    }
    return m_inner->OpenFile(NormalizeVirtualPath(pathname), flags, mode);
}

int ReadOnlyFileSystem::LocalCreateDirectory(const std::string& /*pathname*/, int /*mode*/) {
    return -1;
}

int ReadOnlyFileSystem::LocalRemoveDirectory(const std::string& /*pathname*/) {
    return -1;
}

int ReadOnlyFileSystem::LocalRemoveFile(const std::string& /*pathname*/) {
    return -1;
}

// A read-only filesystem changes nothing: renaming and setting times are as
// much writes as creating and removing.
int ReadOnlyFileSystem::LocalRename(const std::string& /*oldPath*/, const std::string& /*newPath*/) {
    return kFileSystemError;
}

int ReadOnlyFileSystem::LocalSetTimes(const std::string& /*path*/,
                                      const std::optional<FileDateTime>& /*accessTime*/,
                                      const std::optional<FileDateTime>& /*modificationTime*/) {
    return kFileSystemError;
}

std::vector<DirectoryEntry> ReadOnlyFileSystem::LocalReadDirectory(const std::string& path) {
    return m_inner->ReadDirectory(NormalizeVirtualPath(path));
}

int ReadOnlyFileSystem::LocalStat(const std::string& path, FileStatus& out) {
    return m_inner->Stat(NormalizeVirtualPath(path), out);
}

std::optional<std::string> ReadOnlyFileSystem::LocalIsBuiltinCommand(const std::string& path) {
    return m_inner->IsBuiltinCommand(NormalizeVirtualPath(path));
}

std::string ReadOnlyFileSystem::AbsolutePathFor(const std::string& path) const {
    return NormalizeVirtualPath(path);
}

}
