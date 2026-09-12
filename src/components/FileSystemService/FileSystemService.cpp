#include "FileSystemService.h"
#include "src/components/Filesystem/ReadOnlyFileSystem.h"
#include "src/components/Filesystem/InMemoryFileSystem.h"
#include "src/components/Filesystem/SubFileSystem.h"
#include "src/components/Filesystem/MountedFileSystem.h"

namespace Haisos {

std::shared_ptr<IFileSystem> FileSystemService::CreateReadOnlyFileSystem(std::shared_ptr<IFileSystem> filesystem) {
    return std::make_shared<ReadOnlyFileSystem>(std::move(filesystem));
}

std::shared_ptr<IFileSystem> FileSystemService::CreateEmptyInMemFileSystem() {
    return std::make_shared<InMemoryFileSystem>();
}

std::shared_ptr<IFileSystem> FileSystemService::CreateSubFileSystem(std::shared_ptr<IFileSystem> root, const std::string& path) {
    return std::make_shared<SubFileSystem>(std::move(root), path);
}

std::shared_ptr<IFileSystem> FileSystemService::MountFileSystem(std::shared_ptr<IFileSystem> main, const std::string& whereToMount, std::shared_ptr<IFileSystem> toBeMounted) {
    return std::make_shared<MountedFileSystem>(std::move(main), whereToMount, std::move(toBeMounted));
}

}
