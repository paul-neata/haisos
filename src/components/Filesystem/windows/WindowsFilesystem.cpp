#include "Filesystem.h"
#include <memory>
#include <io.h>
#include <direct.h>
#include <windows.h>
#undef CreateDirectory
#undef RemoveDirectory
#undef GetCurrentDirectory
#include <string>
#include <algorithm>

namespace Haisos {

int Filesystem::OpenFile(const std::string& pathname, int flags) {
    return ::_open(pathname.c_str(), flags);
}

int Filesystem::OpenFile(const std::string& pathname, int flags, int mode) {
    return ::_open(pathname.c_str(), flags, mode);
}

int Filesystem::CloseFile(int fd) {
    return ::_close(fd);
}

int Filesystem::ReadFile(int fd, void* buf, size_t count) {
    return ::_read(fd, buf, static_cast<unsigned int>(count));
}

int Filesystem::WriteFile(int fd, const void* buf, size_t count) {
    return ::_write(fd, buf, static_cast<unsigned int>(count));
}

int Filesystem::CreateDirectory(const std::string& pathname, int /*mode*/) {
    return ::_mkdir(pathname.c_str());
}

int Filesystem::RemoveDirectory(const std::string& pathname) {
    return ::_rmdir(pathname.c_str());
}

int Filesystem::ChangeDirectory(const std::string& path) {
    return ::_chdir(path.c_str());
}

char* Filesystem::GetCurrentDirectory(std::string& buf, size_t size) {
    buf.resize(size);
    char* result = ::_getcwd(buf.data(), static_cast<int>(size));
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

    std::string searchPath = path + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = ::FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return entries;
    }

    do {
        if (std::strcmp(fd.cFileName, ".") == 0 || std::strcmp(fd.cFileName, "..") == 0) {
            continue;
        }

        DirectoryEntry de;
        de.name = fd.cFileName;
        de.type = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DirectoryEntryType::Dir : DirectoryEntryType::File;
        entries.push_back(std::move(de));
    } while (::FindNextFileA(hFind, &fd));

    ::FindClose(hFind);
    return entries;
}

std::unique_ptr<IFileSystem> CreateFilesystem() {
    return std::make_unique<Filesystem>();
}

} // namespace Haisos
