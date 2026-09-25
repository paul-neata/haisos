#include "Filesystem.h"
#include <memory>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstring>

namespace Haisos {

int FileSystem::LocalOpenFile(const std::string& pathname, int flags) {
    return ::open(pathname.c_str(), flags);
}

int FileSystem::LocalOpenFile(const std::string& pathname, int flags, int mode) {
    return ::open(pathname.c_str(), flags, static_cast<mode_t>(mode));
}

int FileSystem::LocalCloseFile(int fd) {
    return ::close(fd);
}

ssize_t FileSystem::LocalReadFile(int fd, void* buf, size_t count) {
    return ::read(fd, buf, count);
}

ssize_t FileSystem::LocalWriteFile(int fd, const void* buf, size_t count) {
    return ::write(fd, buf, count);
}

int FileSystem::LocalCreateDirectory(const std::string& pathname, int mode) {
    return ::mkdir(pathname.c_str(), static_cast<mode_t>(mode));
}

int FileSystem::LocalRemoveDirectory(const std::string& pathname) {
    return ::rmdir(pathname.c_str());
}

int FileSystem::LocalRemoveFile(const std::string& pathname) {
    return ::unlink(pathname.c_str());
}

std::vector<DirectoryEntry> FileSystem::LocalReadDirectory(const std::string& path) {
    std::vector<DirectoryEntry> entries;
    DIR* dir = ::opendir(path.c_str());
    if (!dir) {
        return entries;
    }

    struct dirent* entry = nullptr;
    while ((entry = ::readdir(dir)) != nullptr) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        DirectoryEntry de;
        de.name = entry->d_name;

        if (entry->d_type == DT_REG) {
            de.type = DirectoryEntryType::File;
        } else if (entry->d_type == DT_DIR) {
            de.type = DirectoryEntryType::Dir;
        } else {
            struct stat st;
            std::string fullPath = path + "/" + entry->d_name;
            if (::stat(fullPath.c_str(), &st) == 0) {
                de.type = S_ISDIR(st.st_mode) ? DirectoryEntryType::Dir : DirectoryEntryType::File;
            } else {
                de.type = DirectoryEntryType::File;
            }
        }
        entries.push_back(std::move(de));
    }

    ::closedir(dir);
    return entries;
}

int FileSystem::LocalStat(const std::string& path, FileStatus& out) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        return -1;
    }
    out.type = S_ISDIR(st.st_mode) ? DirectoryEntryType::Dir : DirectoryEntryType::File;
    out.size = static_cast<uint64_t>(st.st_size);
    out.blocks = static_cast<uint64_t>(st.st_blocks);
    out.linkCount = static_cast<uint64_t>(st.st_nlink);
    out.accessTime = FileDateTime{static_cast<int64_t>(st.st_atim.tv_sec), static_cast<uint32_t>(st.st_atim.tv_nsec)};
    out.modificationTime = FileDateTime{static_cast<int64_t>(st.st_mtim.tv_sec), static_cast<uint32_t>(st.st_mtim.tv_nsec)};
    out.changeTime = FileDateTime{static_cast<int64_t>(st.st_ctim.tv_sec), static_cast<uint32_t>(st.st_ctim.tv_nsec)};
    return 0;
}

std::shared_ptr<IFileSystem> CreateFilesystem() {
    return FileSystem::Create();
}

} // namespace Haisos
