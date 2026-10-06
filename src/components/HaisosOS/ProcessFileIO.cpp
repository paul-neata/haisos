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

std::shared_ptr<IFileDescriptor> ProcessFileIO::OpenFile(const std::string& pathname, int flags) {
    auto fs = RootFileSystem();
    return fs ? fs->OpenFile(ResolvePath(pathname), flags) : nullptr;
}

std::shared_ptr<IFileDescriptor> ProcessFileIO::OpenFile(const std::string& pathname, int flags, int mode) {
    auto fs = RootFileSystem();
    return fs ? fs->OpenFile(ResolvePath(pathname), flags, mode) : nullptr;
}

// --- The descriptor table ---
// None of these reaches the OS: a file already open works on its own. Every
// shared_ptr dropped -- by CloseDescriptor, Dup2 replacing a slot, or
// ReleaseAllDescriptors -- is moved into a local under the lock and let go of
// only after unlocking: a descriptor's destructor may later block or call back
// (a pipe end waking its peer, a console flushing), and it must never run
// under the table's mutex.

std::shared_ptr<IFileDescriptor> ProcessFileIO::GetDescriptor(int fd) const {
    std::lock_guard<std::mutex> lock(m_descriptorsMutex);
    if (fd < 0 || static_cast<size_t>(fd) >= m_descriptors.size()) {
        return nullptr;
    }
    return m_descriptors[static_cast<size_t>(fd)];
}

int ProcessFileIO::AddDescriptor(std::shared_ptr<IFileDescriptor> descriptor) {
    if (!descriptor) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(m_descriptorsMutex);
    for (size_t i = 0; i < m_descriptors.size(); ++i) {
        if (!m_descriptors[i]) {
            m_descriptors[i] = std::move(descriptor);
            return static_cast<int>(i);
        }
    }
    if (m_descriptors.size() >= static_cast<size_t>(IFileIO::kMaxDescriptors)) {
        return -1;
    }
    m_descriptors.push_back(std::move(descriptor));
    return static_cast<int>(m_descriptors.size() - 1);
}

int ProcessFileIO::Dup(int fd) {
    std::lock_guard<std::mutex> lock(m_descriptorsMutex);
    if (fd < 0 || static_cast<size_t>(fd) >= m_descriptors.size() || !m_descriptors[static_cast<size_t>(fd)]) {
        return -1;
    }
    auto descriptor = m_descriptors[static_cast<size_t>(fd)];
    for (size_t i = 0; i < m_descriptors.size(); ++i) {
        if (!m_descriptors[i]) {
            m_descriptors[i] = std::move(descriptor);
            return static_cast<int>(i);
        }
    }
    if (m_descriptors.size() >= static_cast<size_t>(IFileIO::kMaxDescriptors)) {
        return -1;
    }
    m_descriptors.push_back(std::move(descriptor));
    return static_cast<int>(m_descriptors.size() - 1);
}

int ProcessFileIO::Dup2(int oldFd, int newFd) {
    std::shared_ptr<IFileDescriptor> evicted;
    {
        std::lock_guard<std::mutex> lock(m_descriptorsMutex);
        if (oldFd < 0 || static_cast<size_t>(oldFd) >= m_descriptors.size() || !m_descriptors[static_cast<size_t>(oldFd)]) {
            return -1;
        }
        if (newFd < 0 || static_cast<size_t>(newFd) >= static_cast<size_t>(IFileIO::kMaxDescriptors)) {
            return -1;
        }
        if (oldFd == newFd) {
            return newFd;
        }
        if (static_cast<size_t>(newFd) >= m_descriptors.size()) {
            m_descriptors.resize(static_cast<size_t>(newFd) + 1);
        }
        evicted = std::move(m_descriptors[static_cast<size_t>(newFd)]);
        m_descriptors[static_cast<size_t>(newFd)] = m_descriptors[static_cast<size_t>(oldFd)];
    }
    evicted.reset();
    return newFd;
}

int ProcessFileIO::CloseDescriptor(int fd) {
    std::shared_ptr<IFileDescriptor> evicted;
    {
        std::lock_guard<std::mutex> lock(m_descriptorsMutex);
        if (fd < 0 || static_cast<size_t>(fd) >= m_descriptors.size() || !m_descriptors[static_cast<size_t>(fd)]) {
            return -1;
        }
        evicted = std::move(m_descriptors[static_cast<size_t>(fd)]);
        m_descriptors[static_cast<size_t>(fd)] = nullptr;
    }
    evicted.reset();
    return 0;
}

bool ProcessFileIO::InstallStandardStreams(std::shared_ptr<IFileDescriptor> in,
                                           std::shared_ptr<IFileDescriptor> out,
                                           std::shared_ptr<IFileDescriptor> err) {
    if (!in || !out || !err) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_descriptorsMutex);
    for (const auto& slot : m_descriptors) {
        if (slot) {
            return false;
        }
    }
    // The table is empty: three pushes land in slots 0, 1 and 2.
    m_descriptors.clear();
    m_descriptors.push_back(std::move(in));
    m_descriptors.push_back(std::move(out));
    m_descriptors.push_back(std::move(err));
    return true;
}

std::optional<std::pair<int, int>> ProcessFileIO::CreatePipe(size_t capacity) {
    auto os = m_os.lock();
    if (!os) {
        return std::nullopt;
    }
    auto service = os->GetPipeService();
    if (!service) {
        return std::nullopt;
    }
    // Placed in the table first: should fewer than two slots be free, ends
    // goes away here and the pipe is closed with it, the table left as it was.
    PipeEnds ends = service->CreatePipe(capacity);
    // AddDescriptor takes the lowest free slot, so this gives the two lowest
    // free slots, read end first.
    const int readSlot = AddDescriptor(ends.readEnd);
    if (readSlot < 0) {
        return std::nullopt;
    }
    const int writeSlot = AddDescriptor(ends.writeEnd);
    if (writeSlot < 0) {
        CloseDescriptor(readSlot);
        return std::nullopt;
    }
    return std::make_pair(readSlot, writeSlot);
}

void ProcessFileIO::ReleaseAllDescriptors() {
    std::vector<std::shared_ptr<IFileDescriptor>> released;
    {
        std::lock_guard<std::mutex> lock(m_descriptorsMutex);
        released.swap(m_descriptors);
    }
    released.clear();
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
