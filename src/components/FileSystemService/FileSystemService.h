#pragma once
#include <memory>
#include "interfaces/IServicesCreator.h"

namespace Haisos {

class FileSystemService : public IFilesystemService {
public:
    explicit FileSystemService(std::unique_ptr<IFileSystem> filesystem);
    ~FileSystemService() override;

    IFileSystem& GetFileSystem() override;

private:
    std::unique_ptr<IFileSystem> m_filesystem;
};

}
