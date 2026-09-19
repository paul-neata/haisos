#include "ProcessFileIO.h"
#include "src/components/Filesystem/VirtualPath.h"

namespace Haisos {

std::shared_ptr<ProcessFileIO> ProcessFileIO::Create(
    std::weak_ptr<IHaisosOS> os, const std::string& workingDirectory)
{
    return std::shared_ptr<ProcessFileIO>(new ProcessFileIO(std::move(os), workingDirectory));
}

ProcessFileIO::ProcessFileIO(std::weak_ptr<IHaisosOS> os, const std::string& workingDirectory)
    : m_os(std::move(os))
    // An empty working directory means the OS's root, which is also where a
    // process that never asked to be anywhere else starts.
    , m_workingDirectory(workingDirectory.empty() ? std::string("/") : NormalizeVirtualPath(workingDirectory))
{
}

ProcessFileIO::~ProcessFileIO() = default;

std::shared_ptr<IFileSystem> ProcessFileIO::RootFileSystem() const {
    auto os = m_os.lock();
    return os ? os->GetRootFileSystem() : nullptr;
}

std::string ProcessFileIO::GetCurrentDirectory() const {
    std::lock_guard<std::mutex> lock(m_workingDirectoryMutex);
    return m_workingDirectory;
}

std::string ProcessFileIO::ResolvePath(const std::string& path) const {
    return NormalizeVirtualPath(path, GetCurrentDirectory());
}

int ProcessFileIO::ChangeDirectory(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_workingDirectoryMutex);
    std::string target = NormalizeVirtualPath(path, m_workingDirectory);

    // "/" is always reachable: it is the root the process can never step
    // outside of, and NormalizeVirtualPath drops a leading ".." rather than
    // escaping, so there is nowhere above it to land.
    if (target != "/") {
        auto fs = RootFileSystem();
        if (!fs) {
            return -1;
        }
        // A directory listing of the parent is what tells a directory from a
        // file: a file lists as nothing, and so does a path that is not there.
        auto lastSlash = target.find_last_of('/');
        auto parent = target.substr(0, lastSlash);
        auto name = target.substr(lastSlash + 1);
        bool isDirectory = false;
        for (const auto& entry : fs->ReadDirectory(parent.empty() ? "/" : parent)) {
            if (entry.name == name && entry.type == DirectoryEntryType::Dir) {
                isDirectory = true;
                break;
            }
        }
        if (!isDirectory) {
            return -1;
        }
    }

    m_workingDirectory = target;
    return 0;
}

int ProcessFileIO::OpenFile(const std::string& pathname, int flags) {
    auto fs = RootFileSystem();
    return fs ? fs->OpenFile(ResolvePath(pathname), flags) : -1;
}

int ProcessFileIO::OpenFile(const std::string& pathname, int flags, int mode) {
    auto fs = RootFileSystem();
    return fs ? fs->OpenFile(ResolvePath(pathname), flags, mode) : -1;
}

int ProcessFileIO::CloseFile(int fd) {
    auto fs = RootFileSystem();
    return fs ? fs->CloseFile(fd) : -1;
}

ssize_t ProcessFileIO::ReadFile(int fd, void* buf, size_t count) {
    auto fs = RootFileSystem();
    return fs ? fs->ReadFile(fd, buf, count) : -1;
}

ssize_t ProcessFileIO::WriteFile(int fd, const void* buf, size_t count) {
    auto fs = RootFileSystem();
    return fs ? fs->WriteFile(fd, buf, count) : -1;
}

int ProcessFileIO::CreateDirectory(const std::string& pathname, int mode) {
    auto fs = RootFileSystem();
    return fs ? fs->CreateDirectory(ResolvePath(pathname), mode) : -1;
}

int ProcessFileIO::RemoveDirectory(const std::string& pathname) {
    auto fs = RootFileSystem();
    return fs ? fs->RemoveDirectory(ResolvePath(pathname)) : -1;
}

std::vector<DirectoryEntry> ProcessFileIO::ReadDirectory(const std::string& path) {
    auto fs = RootFileSystem();
    return fs ? fs->ReadDirectory(ResolvePath(path)) : std::vector<DirectoryEntry>{};
}

}
