#pragma once
#include <memory>
#include "interfaces/INetworkService.h"

namespace Haisos {

class NetworkService : public INetworkService {
public:
    static std::shared_ptr<NetworkService> Create();
    ~NetworkService() override;

    std::shared_ptr<IHTTPClient> CreateHTTPClient() override;

private:
    NetworkService();
};

}
