#pragma once
#include <memory>
#include "interfaces/INetworkService.h"

#ifdef _WIN32

namespace Haisos {

class WinHTTPClient : public IHTTPClient {
public:
    static std::shared_ptr<WinHTTPClient> Create() {
        return std::shared_ptr<WinHTTPClient>(new WinHTTPClient());
    }
    ~WinHTTPClient() override;

    // IHTTPClient interface
    HTTPResponse Get(const std::string& url) override;
    HTTPResponse Post(const std::string& url, const std::string& body) override;
    HTTPResponse Post(const std::string& url, const std::string& body, const std::vector<HTTPHeader>& headers) override;

private:
    WinHTTPClient();

    HTTPResponse PerformRequest(const std::string& url, const wchar_t* method, const std::string& body = "", const std::vector<HTTPHeader>& headers = {});

    struct WinHTTPHandle;
    std::unique_ptr<WinHTTPHandle> m_handle;
};

}
#endif
