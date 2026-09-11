#include "FileSystemService.h"

namespace Haisos {

FileSystemService::FileSystemService(std::unique_ptr<IFileSystem> filesystem)
    : m_filesystem(std::move(filesystem))
{
}

FileSystemService::~FileSystemService() = default;

IFileSystem& FileSystemService::GetFileSystem() {
    return *m_filesystem;
}

}
