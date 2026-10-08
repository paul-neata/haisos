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

std::shared_ptr<IFileDescriptor> ComposedFileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    return m_main->OpenFile(AbsolutePathFor(pathname), flags);
}

std::shared_ptr<IFileDescriptor> ComposedFileSystem::LocalOpenFile(const std::string& pathname, int flags, int mode) {
    return m_main->OpenFile(AbsolutePathFor(pathname), flags, mode);
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

int ComposedFileSystem::LocalRename(const std::string& oldPath, const std::string& newPath) {
    return m_main->Rename(AbsolutePathFor(oldPath), AbsolutePathFor(newPath));
}

int ComposedFileSystem::LocalSetTimes(const std::string& path,
                                      const std::optional<FileDateTime>& accessTime,
                                      const std::optional<FileDateTime>& modificationTime) {
    return m_main->SetTimes(AbsolutePathFor(path), accessTime, modificationTime);
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
