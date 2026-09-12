#pragma once
#include <memory>
#include <string>
#include "ILLMService.h"
#include "INetworkService.h"
#include "IFilesystemService.h"
#include "IFactory.h"

namespace Haisos {

// Higher-level factory-of-services, built on top of IFactory. Each service's
// creation explicitly takes the other services it depends on (e.g. ILLMService
// needs an INetworkService to talk to the LLM endpoint over).
class IServicesCreator {
public:
    virtual ~IServicesCreator() = default;

    virtual std::unique_ptr<IFilesystemService> CreateFileSystemService() = 0;
    virtual std::unique_ptr<INetworkService> CreateNetworkService() = 0;
    virtual std::unique_ptr<ILLMService> CreateLLMService(
        INetworkService& networkService,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey) = 0;
};

std::unique_ptr<IServicesCreator> CreateServicesCreator(IFactory& factory);

}
