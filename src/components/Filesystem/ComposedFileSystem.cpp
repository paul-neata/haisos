#include "ComposedFileSystem.h"
#include "VirtualPath.h"

namespace Haisos {

std::shared_ptr<ComposedFileSystem> ComposedFileSystem::Create(
    std::shared_ptr<IFileSystem> main,
    const std::string& whereToMount,
    std::shared_ptr<IFileSystem> mounted)
{
    return std::shared_ptr<ComposedFileSystem>(new ComposedFileSystem(std::move(main), whereToMount, std::move(mounted)));
}

ComposedFileSystem::ComposedFileSystem(
    std::shared_ptr<IFileSystem> main,
    const std::string& whereToMount,
    std::shared_ptr<IFileSystem> mounted)
    : m_main(std::move(main))
{
    Mount(whereToMount, std::move(mounted));
}

ComposedFileSystem::~ComposedFileSystem() = default;

std::string ComposedFileSystem::AbsolutePathFor(const std::string& path) const {
    return NormalizeVirtualPath(path);
}

int ComposedFileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    return m_main->OpenFile(AbsolutePathFor(pathname), flags);
}

int ComposedFileSystem::LocalOpenFile(const std::string& pathname, int flags, int mode) {
    return m_main->OpenFile(AbsolutePathFor(pathname), flags, mode);
}

int ComposedFileSystem::LocalCloseFile(int fd) {
    return m_main->CloseFile(fd);
}

ssize_t ComposedFileSystem::LocalReadFile(int fd, void* buf, size_t count) {
    return m_main->ReadFile(fd, buf, count);
}

ssize_t ComposedFileSystem::LocalWriteFile(int fd, const void* buf, size_t count) {
    return m_main->WriteFile(fd, buf, count);
}

int ComposedFileSystem::LocalCreateDirectory(const std::string& pathname, int mode) {
    return m_main->CreateDirectory(AbsolutePathFor(pathname), mode);
}

int ComposedFileSystem::LocalRemoveDirectory(const std::string& pathname) {
    return m_main->RemoveDirectory(AbsolutePathFor(pathname));
}

int ComposedFileSystem::LocalRemoveFile(const std::string& pathname) {
    return m_main->RemoveFile(AbsolutePathFor(pathname));
}

std::vector<DirectoryEntry> ComposedFileSystem::LocalReadDirectory(const std::string& path) {
    return m_main->ReadDirectory(AbsolutePathFor(path));
}

int ComposedFileSystem::LocalStat(const std::string& path, FileStatus& out) {
    return m_main->Stat(AbsolutePathFor(path), out);
}

std::optional<std::string> ComposedFileSystem::LocalIsBuiltinCommand(const std::string& path) {
    return m_main->IsBuiltinCommand(AbsolutePathFor(path));
}

}
