#pragma once
#include "interfaces/IHTTPClient.h"

#ifdef __EMSCRIPTEN__

#include <atomic>
#include <mutex>
#include <string>

struct emscripten_fetch_t;

namespace Haisos {

class FetchHTTPClient : public IHTTPClient {
public:
    FetchHTTPClient();
    ~FetchHTTPClient() override;

    // IHTTPClient interface
    HTTPResponse Get(const std::string& url) override;
    HTTPResponse Post(const std::string& url, const std::string& body) override;
    HTTPResponse Post(const std::string& url, const std::string& body, const std::vector<HTTPHeader>& headers) override;

private:
    static void FetchSuccess(emscripten_fetch_t* fetch);
    static void FetchError(emscripten_fetch_t* fetch);

    std::string m_pendingResponse;
    int m_pendingStatusCode = 0;
    std::string m_pendingError;
    std::mutex m_responseMutex;
    std::atomic<bool> m_fetchComplete{false};

    HTTPResponse PerformRequest(const std::string& url, const char* method, const std::string& body, const std::vector<HTTPHeader>& headers);
};

}
#endif
