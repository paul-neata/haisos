#pragma once
#include "interfaces/IServicesCreator.h"

namespace Haisos {

class ServicesCreator : public IServicesCreator {
public:
    ServicesCreator();
    ~ServicesCreator() override;

    std::shared_ptr<IServicesCreator> Clone() const override;
    std::unique_ptr<IFilesystemService> CreateFileSystemService() override;
    std::unique_ptr<INetworkService> CreateNetworkService() override;
    std::unique_ptr<ILLMService> CreateLLMService(
        INetworkService& networkService,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey) override;
};

}
