#pragma once
#include "interfaces/IServicesCreator.h"

namespace Haisos {

class ServicesCreator : public IServicesCreator {
public:
    explicit ServicesCreator(IFactory& factory);
    ~ServicesCreator() override;

    std::unique_ptr<IFilesystemService> CreateFileSystemService(std::unique_ptr<IFileSystem> filesystem) override;
    std::unique_ptr<INetworkService> CreateNetworkService() override;
    std::unique_ptr<ILLMService> CreateLLMService(
        INetworkService& networkService,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey) override;

private:
    IFactory& m_factory;
};

}
