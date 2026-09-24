#pragma once
#include <memory>
#include "interfaces/IServicesCreator.h"

namespace Haisos {

class ServicesCreator : public IServicesCreator {
public:
    static std::shared_ptr<ServicesCreator> Create();
    ~ServicesCreator() override;

    std::shared_ptr<IServicesCreator> Clone() const override;
    std::shared_ptr<IFileSystemService> CreateFileSystemService() override;
    std::shared_ptr<INetworkService> CreateNetworkService() override;
    std::shared_ptr<ILLMService> CreateLLMService(
        std::shared_ptr<INetworkService> networkService,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey) override;

private:
    ServicesCreator();
};

}
