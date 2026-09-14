#pragma once
#include <memory>
#include <string>
#include "interfaces/IFactory.h"

namespace Haisos {

class Factory : public IFactory {
public:
    Factory();
    ~Factory() override;

    // IFactory interface
    std::shared_ptr<IPhysicalConsole> CreatePhysicalConsole(bool registerAsLogMessageReceiver) override;
    std::unique_ptr<IFileSystem> CreatePhysicalFileSystem(const std::string& rootPath) override;
    std::unique_ptr<IServicesCreator> CreateServicesCreator() override;
    std::shared_ptr<IHaisosOS> CreateHaisosOS(
        std::shared_ptr<IServicesCreator> servicesCreator,
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        std::shared_ptr<IFileSystem> rootFileSystem,
        const OSEnvironment& environment,
        uint64_t osProcessId) override;
};

std::unique_ptr<IFactory> CreateFactory();

}
