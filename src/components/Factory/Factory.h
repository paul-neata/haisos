#pragma once
#include <memory>
#include <string>
#include "interfaces/IFactory.h"

namespace Haisos {

class Factory : public IFactory {
public:
    static std::shared_ptr<Factory> Create();
    ~Factory() override;

    // IFactory interface
    std::shared_ptr<IPhysicalConsole> CreatePhysicalConsole(bool registerAsLogMessageReceiver) override;
    std::shared_ptr<IFileSystem> CreatePhysicalFileSystem(const std::string& rootPath) override;
    std::shared_ptr<IEnvironment> CreateEnvironment() override;
    std::shared_ptr<IServicesCreator> CreateServicesCreator() override;
    std::shared_ptr<IHaisosOS> CreateHaisosOS(
        std::shared_ptr<IServicesCreator> servicesCreator,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        std::shared_ptr<IFileSystem> rootFileSystem,
        std::shared_ptr<IEnvironment> environment) override;
    uint64_t GetNextGloballyUniquePID() override;

private:
    Factory();
};

std::shared_ptr<IFactory> CreateFactory();

}
