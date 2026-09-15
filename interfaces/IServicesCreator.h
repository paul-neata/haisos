#pragma once
#include <memory>
#include <string>
#include "ILLMService.h"
#include "INetworkService.h"
#include "IFilesystemService.h"

namespace Haisos {

// Higher-level factory-of-services, built on top of IFactory. Each service's
// creation explicitly takes the other services it depends on (e.g. ILLMService
// needs an INetworkService to talk to the LLM endpoint over).
class IServicesCreator {
public:
    virtual ~IServicesCreator() = default;

    // An independent services creator equivalent to this one. A sub-OS gets its
    // own rather than sharing its parent's, so the services it creates are its
    // own and its lifetime is not tied to the parent's.
    virtual std::shared_ptr<IServicesCreator> Clone() const = 0;

    virtual std::unique_ptr<IFilesystemService> CreateFileSystemService() = 0;
    virtual std::unique_ptr<INetworkService> CreateNetworkService() = 0;
    virtual std::unique_ptr<ILLMService> CreateLLMService(
        INetworkService& networkService,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey) = 0;
};

std::unique_ptr<IServicesCreator> CreateServicesCreator();

}
