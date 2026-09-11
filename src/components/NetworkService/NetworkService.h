#pragma once
#include "interfaces/IServicesCreator.h"

namespace Haisos {

class NetworkService : public INetworkService {
public:
    explicit NetworkService(IFactory& factory);
    ~NetworkService() override;

    std::unique_ptr<IHTTPClient> CreateHTTPClient() override;

private:
    IFactory& m_factory;
};

}
