#include "FetchHTTPClient.h"
#include <emscripten/fetch.h>
#include <string>
#include <mutex>
#include <atomic>

namespace Haisos {

#ifdef __EMSCRIPTEN__

void FetchHTTPClient::FetchSuccess(emscripten_fetch_t* fetch) {
    auto* client = static_cast<FetchHTTPClient*>(fetch->userData);
    std::lock_guard<std::mutex> lock(client->m_responseMutex);
    client->m_pendingResponse.assign(fetch->data, fetch->numBytes);
    client->m_pendingStatusCode = fetch->status;
    client->m_pendingError.clear();
    client->m_fetchComplete = true;
    emscripten_fetch_close(fetch);
}

void FetchHTTPClient::FetchError(emscripten_fetch_t* fetch) {
    auto* client = static_cast<FetchHTTPClient*>(fetch->userData);
    std::lock_guard<std::mutex> lock(client->m_responseMutex);
    client->m_pendingResponse.assign(fetch->data, fetch->numBytes);
    client->m_pendingStatusCode = fetch->status;
    client->m_pendingError = "Error: Fetch failed";
    client->m_fetchComplete = true;
    emscripten_fetch_close(fetch);
}

FetchHTTPClient::FetchHTTPClient() = default;
FetchHTTPClient::~FetchHTTPClient() = default;

HTTPResponse FetchHTTPClient::Get(const std::string& url) {
    return PerformRequest(url, "GET", "", {});
}

HTTPResponse FetchHTTPClient::Post(const std::string& url, const std::string& body) {
    return PerformRequest(url, "POST", body, {});
}

HTTPResponse FetchHTTPClient::Post(const std::string& url, const std::string& body, const std::vector<HTTPHeader>& headers) {
    return PerformRequest(url, "POST", body, headers);
}

HTTPResponse FetchHTTPClient::PerformRequest(const std::string& url, const char* method, const std::string& body, const std::vector<HTTPHeader>& headers) {
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingResponse.clear();
        m_pendingStatusCode = 0;
        m_pendingError.clear();
    }
    m_fetchComplete = false;

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, method);
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = FetchSuccess;
    attr.onerror = FetchError;
    attr.userData = this;

    std::string requestHeaders;
    for (const auto& h : headers) {
        requestHeaders += h.name + ": " + h.value + "\n";
    }
    if (!requestHeaders.empty()) {
        attr.requestHeaders = reinterpret_cast<const char**>(&requestHeaders);
    }

    if (strcmp(method, "POST") == 0 && !body.empty()) {
        attr.requestData = body.c_str();
        attr.requestDataSize = body.size();
    }

    emscripten_fetch_t* fetch = emscripten_fetch(&attr, url.c_str());

    // Wait for completion (synchronous wait with timeout)
    int waitMs = 120000;
    while (!m_fetchComplete && waitMs > 0) {
        emscripten_fetch_waitUntilCompleted(fetch, 1000);
        waitMs -= 1000;
    }

    HTTPResponse response;
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        response.statusCode = m_pendingStatusCode;
        response.body = m_pendingResponse;
        response.error = m_pendingError;
        m_lastResponse = m_pendingResponse;
    }
    return response;
}

#endif
}
