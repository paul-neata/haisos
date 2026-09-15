#include "NetworkService.h"
#include "src/components/HTTPClient/HTTPClient.h"

namespace Haisos {

NetworkService::NetworkService() = default;
NetworkService::~NetworkService() = default;

std::unique_ptr<IHTTPClient> NetworkService::CreateHTTPClient() {
    return ::Haisos::CreateHTTPClient();
}

}
