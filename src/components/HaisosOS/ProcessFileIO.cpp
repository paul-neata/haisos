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
        FileStatus status;
        if (fs->Stat(target, status) != 0 || status.type != DirectoryEntryType::Dir) {
            return -1;
        }
    }

    m_workingDirectory = target;
    return 0;
}

namespace {

// Interim, until fd--process-table: put |file| into the process's number map
// at the lowest number >= 3 not taken, and return that number.
int AddOpenFile(std::map<int, std::shared_ptr<IFileDescriptor>>& openFiles,
                std::mutex& mutex, std::shared_ptr<IFileDescriptor> file) {
    std::lock_guard<std::mutex> lock(mutex);
    int number = 3;
    while (openFiles.count(number) > 0) {
        ++number;
    }
    openFiles[number] = std::move(file);
    return number;
}

} // namespace

int ProcessFileIO::OpenFile(const std::string& pathname, int flags) {
    auto fs = RootFileSystem();
    if (!fs) {
        return -1;
    }
    auto file = fs->OpenFile(ResolvePath(pathname), flags);
    return file ? AddOpenFile(m_openFiles, m_openFilesMutex, std::move(file)) : -1;
}

int ProcessFileIO::OpenFile(const std::string& pathname, int flags, int mode) {
    auto fs = RootFileSystem();
    if (!fs) {
        return -1;
    }
    auto file = fs->OpenFile(ResolvePath(pathname), flags, mode);
    return file ? AddOpenFile(m_openFiles, m_openFilesMutex, std::move(file)) : -1;
}

// Interim, until fd--process-table: the three below look the descriptor up by
// its number and act on the object itself -- the OS is not needed any more,
// since a descriptor works on its own. The shared_ptr is taken out under the
// mutex and used after it is released, so a CloseFile racing a ReadFile does
// not tear the descriptor down beneath it.
int ProcessFileIO::CloseFile(int fd) {
    std::shared_ptr<IFileDescriptor> file;
    {
        std::lock_guard<std::mutex> lock(m_openFilesMutex);
        auto it = m_openFiles.find(fd);
        if (it == m_openFiles.end()) {
            return -1;
        }
        file = std::move(it->second);
        m_openFiles.erase(it);
    }
    file.reset();
    return 0;
}

ssize_t ProcessFileIO::ReadFile(int fd, void* buf, size_t count) {
    std::shared_ptr<IFileDescriptor> file;
    {
        std::lock_guard<std::mutex> lock(m_openFilesMutex);
        auto it = m_openFiles.find(fd);
        if (it == m_openFiles.end()) {
            return -1;
        }
        file = it->second;
    }
    return file->Read(buf, count);
}

ssize_t ProcessFileIO::WriteFile(int fd, const void* buf, size_t count) {
    std::shared_ptr<IFileDescriptor> file;
    {
        std::lock_guard<std::mutex> lock(m_openFilesMutex);
        auto it = m_openFiles.find(fd);
        if (it == m_openFiles.end()) {
            return -1;
        }
        file = it->second;
    }
    return file->Write(buf, count);
}

int ProcessFileIO::CreateDirectory(const std::string& pathname, int mode) {
    auto fs = RootFileSystem();
    return fs ? fs->CreateDirectory(ResolvePath(pathname), mode) : -1;
}

int ProcessFileIO::RemoveDirectory(const std::string& pathname) {
    auto fs = RootFileSystem();
    return fs ? fs->RemoveDirectory(ResolvePath(pathname)) : -1;
}

int ProcessFileIO::RemoveFile(const std::string& pathname) {
    auto fs = RootFileSystem();
    return fs ? fs->RemoveFile(ResolvePath(pathname)) : -1;
}

std::vector<DirectoryEntry> ProcessFileIO::ReadDirectory(const std::string& path) {
    auto fs = RootFileSystem();
    return fs ? fs->ReadDirectory(ResolvePath(path)) : std::vector<DirectoryEntry>{};
}

int ProcessFileIO::Stat(const std::string& path, FileStatus& out) {
    auto fs = RootFileSystem();
    return fs ? fs->Stat(ResolvePath(path), out) : -1;
}

std::optional<std::string> ProcessFileIO::IsBuiltinCommand(const std::string& path) {
    auto fs = RootFileSystem();
    return fs ? fs->IsBuiltinCommand(ResolvePath(path)) : std::nullopt;
}

}
