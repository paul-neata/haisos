#include "Factory.h"
#include "src/components/Console/Console.h"
#include "src/components/Environment/Environment.h"
#include "src/components/Filesystem/PhysicalFileSystem.h"
#include "src/components/HaisosOS/HaisosOS.h"
#include "src/components/ServicesCreator/ServicesCreator.h"

namespace Haisos {

Factory::Factory() = default;
Factory::~Factory() = default;

std::shared_ptr<IPhysicalConsole> Factory::CreatePhysicalConsole(bool registerAsLogMessageReceiver) {
    return std::make_shared<Console>(registerAsLogMessageReceiver);
}

std::unique_ptr<IFileSystem> Factory::CreatePhysicalFileSystem(const std::string& rootPath) {
    return std::make_unique<PhysicalFileSystem>(rootPath);
}

std::shared_ptr<IEnvironment> Factory::CreateEnvironment() {
    return ::Haisos::CreateEnvironment();
}

std::unique_ptr<IServicesCreator> Factory::CreateServicesCreator() {
    return ::Haisos::CreateServicesCreator();
}

std::shared_ptr<IHaisosOS> Factory::CreateHaisosOS(
    std::shared_ptr<IServicesCreator> servicesCreator,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    std::shared_ptr<IFileSystem> rootFileSystem,
    std::shared_ptr<IEnvironment> environment,
    uint64_t osProcessId)
{
    return ::Haisos::CreateHaisosOS(
        std::move(servicesCreator), std::move(physicalConsole), std::move(rootFileSystem), std::move(environment), osProcessId);
}

std::unique_ptr<IFactory> CreateFactory() {
    return std::make_unique<Factory>();
}

}
