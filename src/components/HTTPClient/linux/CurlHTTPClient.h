#pragma once
#include <memory>
#include "interfaces/INetworkService.h"

namespace Haisos {

class CurlHTTPClient : public IHTTPClient {
public:
    static std::shared_ptr<CurlHTTPClient> Create() {
        return std::shared_ptr<CurlHTTPClient>(new CurlHTTPClient());
    }
    ~CurlHTTPClient() override;

    // IHTTPClient interface
    HTTPResponse Get(const std::string& url) override;
    HTTPResponse Post(const std::string& url, const std::string& body) override;
    HTTPResponse Post(const std::string& url, const std::string& body, const std::vector<HTTPHeader>& headers) override;

private:
    CurlHTTPClient();

    HTTPResponse PerformRequest(const std::string& url, const std::string& method, const std::string& body = "", const std::vector<HTTPHeader>& headers = {});
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

    struct CurlHandle;
    std::unique_ptr<CurlHandle> m_handle;
};

}
