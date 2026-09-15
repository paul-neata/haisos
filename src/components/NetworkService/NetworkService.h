#pragma once
#include "interfaces/INetworkService.h"

namespace Haisos {

class NetworkService : public INetworkService {
public:
    NetworkService();
    ~NetworkService() override;

    std::unique_ptr<IHTTPClient> CreateHTTPClient() override;
};

}
