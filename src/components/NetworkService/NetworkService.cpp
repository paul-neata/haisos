#include "NetworkService.h"

namespace Haisos {

NetworkService::NetworkService(IFactory& factory) : m_factory(factory) {}
NetworkService::~NetworkService() = default;

std::unique_ptr<IHTTPClient> NetworkService::CreateHTTPClient() {
    return m_factory.CreateHTTPClient();
}

}
