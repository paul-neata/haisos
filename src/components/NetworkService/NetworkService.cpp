#include "NetworkService.h"
#include "src/components/HTTPClient/HTTPClient.h"

namespace Haisos {

std::shared_ptr<NetworkService> NetworkService::Create() {
    return std::shared_ptr<NetworkService>(new NetworkService());
}

NetworkService::NetworkService() = default;
NetworkService::~NetworkService() = default;

std::shared_ptr<IHTTPClient> NetworkService::CreateHTTPClient() {
    return ::Haisos::CreateHTTPClient();
}

}
