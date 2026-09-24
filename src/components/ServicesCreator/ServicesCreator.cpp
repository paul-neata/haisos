#include "ServicesCreator.h"
#include "src/components/NetworkService/NetworkService.h"
#include "src/components/FileSystemService/FileSystemService.h"
#include "src/components/LLMService/LLMService.h"

namespace Haisos {

std::shared_ptr<ServicesCreator> ServicesCreator::Create() {
    return std::shared_ptr<ServicesCreator>(new ServicesCreator());
}

ServicesCreator::ServicesCreator() = default;
ServicesCreator::~ServicesCreator() = default;

std::shared_ptr<IServicesCreator> ServicesCreator::Clone() const {
    // ServicesCreator holds no state of its own, so a clone is simply a new one.
    return ServicesCreator::Create();
}

std::shared_ptr<IFileSystemService> ServicesCreator::CreateFileSystemService() {
    return FileSystemService::Create();
}

std::shared_ptr<INetworkService> ServicesCreator::CreateNetworkService() {
    return NetworkService::Create();
}

std::shared_ptr<ILLMService> ServicesCreator::CreateLLMService(
    std::shared_ptr<INetworkService> networkService,
    const std::string& endpoint,
    const std::string& modelName,
    const std::string& apiKey)
{
    return LLMService::Create(std::move(networkService), endpoint, modelName, apiKey);
}

std::shared_ptr<IServicesCreator> CreateServicesCreator() {
    return ServicesCreator::Create();
}

}
