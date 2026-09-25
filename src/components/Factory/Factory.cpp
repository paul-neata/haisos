#include "Factory.h"
#include "src/components/BuiltinCommands/BuiltinCommands.h"
#include "src/components/BuiltinCommands/BuiltinConfigurator.h"
#include "src/components/Console/Console.h"
#include "src/components/Environment/Environment.h"
#include "src/components/Filesystem/PhysicalFileSystem.h"
#include "src/components/HaisosOS/HaisosOS.h"
#include "src/components/ServicesCreator/ServicesCreator.h"
#include "src/components/libheaders/GloballyUniquePID.h"

namespace Haisos {

std::shared_ptr<Factory> Factory::Create() {
    return std::shared_ptr<Factory>(new Factory());
}

Factory::Factory() = default;
Factory::~Factory() = default;

std::shared_ptr<IPhysicalConsole> Factory::CreatePhysicalConsole() {
    return Console::Create();
}

std::shared_ptr<IFileSystem> Factory::CreatePhysicalFileSystem(const std::string& rootPath) {
    return PhysicalFileSystem::Create(rootPath);
}

std::shared_ptr<IEnvironment> Factory::CreateEnvironment() {
    return ::Haisos::CreateEnvironment();
}

std::shared_ptr<IServicesCreator> Factory::CreateServicesCreator() {
    return ::Haisos::CreateServicesCreator();
}

std::shared_ptr<IBuiltinCommands> Factory::CreateBuiltinCommands() {
    return BuiltinCommands::Create();
}

std::shared_ptr<IBuiltinConfigurator> Factory::CreateBuiltinConfigurator() {
    return BuiltinConfigurator::Create();
}

std::shared_ptr<IHaisosOS> Factory::CreateHaisosOS(
    std::shared_ptr<IServicesCreator> servicesCreator,
    std::shared_ptr<IPhysicalConsole> physicalConsole,
    std::shared_ptr<IFileSystem> rootFileSystem,
    std::shared_ptr<IBuiltinCommands> builtinCommands,
    std::shared_ptr<IEnvironment> environment)
{
    // Every OS created here is a root of its own tree, so it is given a pid of
    // its own rather than borrowing one; only CreateSubOS passes an existing pid on.
    return HaisosOS::Create(
        std::move(servicesCreator),
        std::move(physicalConsole),
        std::move(rootFileSystem),
        std::move(builtinCommands),
        std::move(environment),
        GetNextGloballyUniquePID());
}

uint64_t Factory::GetNextGloballyUniquePID() {
    return NextGloballyUniquePID();
}

std::shared_ptr<IFactory> CreateFactory() {
    return Factory::Create();
}

}
