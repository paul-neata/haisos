#include "FileSystemService.h"
#include "src/components/Filesystem/ReadOnlyFileSystem.h"
#include "src/components/Filesystem/InMemoryFileSystem.h"
#include "src/components/Filesystem/SubFileSystem.h"
#include "src/components/Filesystem/ComposedFileSystem.h"

namespace Haisos {

std::shared_ptr<IFileSystem> FileSystemService::CreateReadOnlyFileSystem(std::shared_ptr<IFileSystem> filesystem) {
    return ReadOnlyFileSystem::Create(std::move(filesystem));
}

std::shared_ptr<IFileSystem> FileSystemService::CreateEmptyInMemFileSystem() {
    return InMemoryFileSystem::Create();
}

std::shared_ptr<IFileSystem> FileSystemService::CreateSubFileSystem(std::shared_ptr<IFileSystem> root, const std::string& path) {
    return SubFileSystem::Create(std::move(root), path);
}

std::shared_ptr<IFileSystem> FileSystemService::CreateComposedFileSystem(std::shared_ptr<IFileSystem> main, const std::string& whereToMount, std::shared_ptr<IFileSystem> toBeMounted) {
    return ComposedFileSystem::Create(std::move(main), whereToMount, std::move(toBeMounted));
}

}
