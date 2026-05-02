#include "Filesystem.h"
#include <memory>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstring>

namespace Haisos {

int FileSystem::OpenFile(const std::string& pathname, int flags) {
    return ::open(pathname.c_str(), flags);
}

int FileSystem::OpenFile(const std::string& pathname, int flags, int mode) {
    return ::open(pathname.c_str(), flags, static_cast<mode_t>(mode));
}

int FileSystem::CloseFile(int fd) {
    return ::close(fd);
}

ssize_t FileSystem::ReadFile(int fd, void* buf, size_t count) {
    return ::read(fd, buf, count);
}

ssize_t FileSystem::WriteFile(int fd, const void* buf, size_t count) {
    return ::write(fd, buf, count);
}

int FileSystem::CreateDirectory(const std::string& pathname, int mode) {
    return ::mkdir(pathname.c_str(), static_cast<mode_t>(mode));
}

int FileSystem::RemoveDirectory(const std::string& pathname) {
    return ::rmdir(pathname.c_str());
}

int FileSystem::ChangeDirectory(const std::string& path) {
    return ::chdir(path.c_str());
}

char* FileSystem::GetCurrentDirectory(std::string& buf, size_t size) {
    buf.resize(size);
    char* result = ::getcwd(buf.data(), size);
    if (result != nullptr) {
        auto len = std::strlen(buf.data());
        buf.resize(len);
    } else {
        buf.clear();
    }
    return result;
}

std::vector<DirectoryEntry> FileSystem::ReadDirectory(const std::string& path) {
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

std::unique_ptr<IFileSystem> CreateFilesystem() {
    return std::make_unique<FileSystem>();
}

} // namespace Haisos
