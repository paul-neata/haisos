#include "Factory.h"
#include "src/components/Console/Console.h"
#include "src/components/HTTPClient/HTTPClient.h"
#include "src/components/Filesystem/Filesystem.h"
#include "src/components/Filesystem/PhysicalFileSystem.h"

namespace Haisos {

Factory::Factory() = default;
Factory::~Factory() = default;

std::shared_ptr<IPhysicalConsole> Factory::CreatePhysicalConsole(bool registerAsLogMessageReceiver) {
    return std::make_shared<Console>(registerAsLogMessageReceiver);
}

std::unique_ptr<IHTTPClient> Factory::CreateHTTPClient() {
    return ::Haisos::CreateHTTPClient();
}

std::unique_ptr<IFileSystem> Factory::CreateFilesystem() {
    return std::make_unique<FileSystem>();
}

std::unique_ptr<IFileSystem> Factory::CreatePhysicalFileSystem(const std::string& rootPath) {
    return std::make_unique<PhysicalFileSystem>(rootPath);
}

std::unique_ptr<IFactory> CreateFactory() {
    return std::make_unique<Factory>();
}

}
