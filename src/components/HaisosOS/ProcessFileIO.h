#pragma once
#include <memory>
#include <mutex>
#include <string>
#include "interfaces/IFileIO.h"
#include "interfaces/IHaisosOS.h"

namespace Haisos {

// One process's file I/O: the root filesystem of the OS that process was given,
// plus the working directory it resolves relative paths against.
//
// The filesystem is fetched from the OS on every call rather than held, so a
// process handed a narrowed OS does its I/O through that OS's root and nothing
// else -- there is no copy of a wider filesystem left lying around here. The
// reference to the OS is weak, because an OS owns its processes and a process
// owns this.
class ProcessFileIO : public IFileIO {
public:
    static std::shared_ptr<ProcessFileIO> Create(std::weak_ptr<IHaisosOS> os, const std::string& workingDirectory);
    ~ProcessFileIO() override;

    std::string GetCurrentDirectory() const override;
    int ChangeDirectory(const std::string& path) override;
    std::string ResolvePath(const std::string& path) const override;

    int OpenFile(const std::string& pathname, int flags) override;
    int OpenFile(const std::string& pathname, int flags, int mode) override;
    int CloseFile(int fd) override;
    ssize_t ReadFile(int fd, void* buf, size_t count) override;
    ssize_t WriteFile(int fd, const void* buf, size_t count) override;
    int CreateDirectory(const std::string& pathname, int mode) override;
    int RemoveDirectory(const std::string& pathname) override;
    int RemoveFile(const std::string& pathname) override;
    std::vector<DirectoryEntry> ReadDirectory(const std::string& path) override;

private:
    ProcessFileIO(std::weak_ptr<IHaisosOS> os, const std::string& workingDirectory);

    // The filesystem to act on, or null if the OS is gone -- in which case
    // every operation fails the way any other failure would.
    std::shared_ptr<IFileSystem> RootFileSystem() const;

    std::weak_ptr<IHaisosOS> m_os;

    // Guarded: the process's own thread and whoever inspects it run concurrently.
    mutable std::mutex m_workingDirectoryMutex;
    std::string m_workingDirectory;
};

}
