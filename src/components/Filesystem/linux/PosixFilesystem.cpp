#include "Filesystem.h"
#include <memory>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstring>

namespace Haisos {

int Filesystem::OpenFile(const std::string& pathname, int flags) {
    return ::open(pathname.c_str(), flags);
}

int Filesystem::OpenFile(const std::string& pathname, int flags, int mode) {
    return ::open(pathname.c_str(), flags, static_cast<mode_t>(mode));
}

int Filesystem::CloseFile(int fd) {
    return ::close(fd);
}

int Filesystem::ReadFile(int fd, void* buf, size_t count) {
    return static_cast<int>(::read(fd, buf, count));
}

int Filesystem::WriteFile(int fd, const void* buf, size_t count) {
    return static_cast<int>(::write(fd, buf, count));
}

int Filesystem::CreateDirectory(const std::string& pathname, int mode) {
    return ::mkdir(pathname.c_str(), static_cast<mode_t>(mode));
}

int Filesystem::RemoveDirectory(const std::string& pathname) {
    return ::rmdir(pathname.c_str());
}

int Filesystem::ChangeDirectory(const std::string& path) {
    return ::chdir(path.c_str());
}

char* Filesystem::GetCurrentDirectory(std::string& buf, size_t size) {
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

std::vector<DirectoryEntry> Filesystem::ReadDirectory(const std::string& path) {
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
    return std::make_unique<Filesystem>();
}

} // namespace Haisos
