#pragma once
#include <memory>
#include "interfaces/IServicesCreator.h"

namespace Haisos {

class FileSystemService : public IFilesystemService {
public:
    static std::shared_ptr<FileSystemService> Create() {
        return std::shared_ptr<FileSystemService>(new FileSystemService());
    }
    ~FileSystemService() override = default;

    std::shared_ptr<IFileSystem> CreateReadOnlyFileSystem(std::shared_ptr<IFileSystem> filesystem) override;
    std::shared_ptr<IFileSystem> CreateEmptyInMemFileSystem() override;
    std::shared_ptr<IFileSystem> CreateSubFileSystem(std::shared_ptr<IFileSystem> root, const std::string& path) override;
    std::shared_ptr<IFileSystem> CreateComposedFileSystem(std::shared_ptr<IFileSystem> main, const std::string& whereToMount, std::shared_ptr<IFileSystem> toBeMounted) override;

private:
    FileSystemService() = default;
};

}
