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
    std::unique_ptr<IHTTPClient> CreateHTTPClient() override;
    std::unique_ptr<IFileSystem> CreateFilesystem() override;
    std::unique_ptr<IFileSystem> CreatePhysicalFileSystem(const std::string& rootPath) override;
};

std::unique_ptr<IFactory> CreateFactory();

}
