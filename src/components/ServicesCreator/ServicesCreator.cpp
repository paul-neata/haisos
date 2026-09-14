#include "ServicesCreator.h"
#include "src/components/NetworkService/NetworkService.h"
#include "src/components/FileSystemService/FileSystemService.h"
#include "src/components/LLMService/LLMService.h"

namespace Haisos {

ServicesCreator::ServicesCreator() = default;
ServicesCreator::~ServicesCreator() = default;

std::unique_ptr<IFilesystemService> ServicesCreator::CreateFileSystemService() {
    return std::make_unique<FileSystemService>();
}

std::unique_ptr<INetworkService> ServicesCreator::CreateNetworkService() {
    return std::make_unique<NetworkService>();
}

std::unique_ptr<ILLMService> ServicesCreator::CreateLLMService(
    INetworkService& networkService,
    const std::string& endpoint,
    const std::string& modelName,
    const std::string& apiKey)
{
    return std::make_unique<LLMService>(networkService, endpoint, modelName, apiKey);
}

std::unique_ptr<IServicesCreator> CreateServicesCreator() {
    return std::make_unique<ServicesCreator>();
}

}
