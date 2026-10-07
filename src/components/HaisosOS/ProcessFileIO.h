#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "interfaces/IFileIO.h"
#include "interfaces/IHaisosOS.h"

namespace Haisos {

// One process's file I/O: the root filesystem of the OS that process was given,
// plus the working directory it resolves relative paths against, and the
// process's descriptor table -- its open files by number, as a POSIX process
// holds them.
//
// The filesystem is fetched from the OS on every call rather than held, so a
// process handed a narrowed OS does its I/O through that OS's root and nothing
// else -- there is no copy of a wider filesystem left lying around here. The
// reference to the OS is weak, because an OS owns its processes and a process
// owns this. The table works without an OS: a file already open no longer
// needs one.
class ProcessFileIO : public IFileIO {
public:
    static std::shared_ptr<ProcessFileIO> Create(std::weak_ptr<IHaisosOS> os, const std::string& workingDirectory);
    ~ProcessFileIO() override;

    std::string GetCurrentDirectory() const override;
    int ChangeDirectory(const std::string& path) override;
    std::string ResolvePath(const std::string& path) const override;

    std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags) override;
    std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags, int mode) override;
    int CreateDirectory(const std::string& pathname, int mode) override;
    int RemoveDirectory(const std::string& pathname) override;
    int RemoveFile(const std::string& pathname) override;
    std::vector<DirectoryEntry> ReadDirectory(const std::string& path) override;
    int Stat(const std::string& path, FileStatus& out) override;
    std::optional<std::string> IsBuiltinCommand(const std::string& path) override;

    std::shared_ptr<IFileDescriptor> GetDescriptor(int fd) const override;
    int AddDescriptor(std::shared_ptr<IFileDescriptor> descriptor) override;
    int Dup(int fd) override;
    int Dup2(int oldFd, int newFd) override;
    int CloseDescriptor(int fd) override;

    // Unlike the other table operations this reaches the OS: the pipe service
    // comes from ICurrentProcess::OS(), the one door out of a process (see the
    // Security section of the root CLAUDE.md) -- nothing in a process holds a
    // pipe service of its own.
    std::optional<std::pair<int, int>> CreatePipe(size_t capacity = 0) override;

    // Places in, out and err in slots 0, 1 and 2 of a table that has no
    // descriptor yet. Returns false, changing nothing, if any is null or the
    // table is not empty. Called by a process's Create() before its program
    // starts.
    bool InstallStandardStreams(std::shared_ptr<IFileDescriptor> in,
                                std::shared_ptr<IFileDescriptor> out,
                                std::shared_ptr<IFileDescriptor> err);

    // Releases every descriptor in the table (the table is left empty and still
    // usable). Called by the process classes when their program ends, before they
    // report finished. Releases outside the lock.
    void ReleaseAllDescriptors();

private:
    ProcessFileIO(std::weak_ptr<IHaisosOS> os, const std::string& workingDirectory);

    // The filesystem to act on, or null if the OS is gone -- in which case
    // every operation fails the way any other failure would.
    std::shared_ptr<IFileSystem> RootFileSystem() const;

    // AddDescriptor, and Dup's placement, with m_descriptorsMutex already
    // held: the lowest free slot, growing the table if needed.
    int AddDescriptorLocked(std::shared_ptr<IFileDescriptor> descriptor);
    // The index of the lowest free slot at or after |from| (a slot past the
    // end counts as free while the table may still grow), or -1 when none.
    // m_descriptorsMutex is held.
    int NextFreeSlotLocked(size_t from) const;

    std::weak_ptr<IHaisosOS> m_os;

    // Guarded: the process's own thread and whoever inspects it run concurrently.
    mutable std::mutex m_workingDirectoryMutex;
    std::string m_workingDirectory;

    mutable std::mutex m_descriptorsMutex;
    // Index = slot; null = free. Grown on demand, never beyond IFileIO::kMaxDescriptors.
    std::vector<std::shared_ptr<IFileDescriptor>> m_descriptors;
};

}
