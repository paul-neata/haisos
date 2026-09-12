#include "ServicesCreator.h"
#include "src/components/NetworkService/NetworkService.h"
#include "src/components/FileSystemService/FileSystemService.h"
#include "src/components/LLMService/LLMService.h"

namespace Haisos {

ServicesCreator::ServicesCreator(IFactory& factory) : m_factory(factory) {}
ServicesCreator::~ServicesCreator() = default;

std::unique_ptr<IFilesystemService> ServicesCreator::CreateFileSystemService() {
    return std::make_unique<FileSystemService>();
}

std::unique_ptr<INetworkService> ServicesCreator::CreateNetworkService() {
    return std::make_unique<NetworkService>(m_factory);
}

std::unique_ptr<ILLMService> ServicesCreator::CreateLLMService(
    INetworkService& networkService,
    const std::string& endpoint,
    const std::string& modelName,
    const std::string& apiKey)
{
    return std::make_unique<LLMService>(networkService, endpoint, modelName, apiKey);
}

std::unique_ptr<IServicesCreator> CreateServicesCreator(IFactory& factory) {
    return std::make_unique<ServicesCreator>(factory);
}

}
