#include "CurlHTTPClient.h"
#include <curl/curl.h>
#include <sstream>
#include <cstring>
#include <memory>
#include "src/components/Logger/Logger.h"

namespace Haisos {

struct CurlHTTPClient::CurlHandle {
    CURL* curl;
    std::string response_data;

    CurlHandle() : curl(curl_easy_init()) {}
    ~CurlHandle() { if (curl) curl_easy_cleanup(curl); }
};

CurlHTTPClient::CurlHTTPClient() : m_handle(std::make_unique<CurlHandle>()) {
}

CurlHTTPClient::~CurlHTTPClient() = default;

size_t CurlHTTPClient::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), realsize);
    return realsize;
}

HTTPResponse CurlHTTPClient::Get(const std::string& url) {
    return PerformRequest(url, "GET");
}

HTTPResponse CurlHTTPClient::Post(const std::string& url, const std::string& body) {
    return PerformRequest(url, "POST", body, {});
}

HTTPResponse CurlHTTPClient::Post(const std::string& url, const std::string& body, const std::vector<HTTPHeader>& headers) {
    return PerformRequest(url, "POST", body, headers);
}

HTTPResponse CurlHTTPClient::PerformRequest(const std::string& url, const std::string& method, const std::string& body, const std::vector<HTTPHeader>& headers) {
    HTTPResponse response;

    LogDebug("HTTPClient %s %s starting", method.c_str(), url.c_str());


    if (!m_handle->curl) {
        response.statusCode = 0;
        response.error = "Error: curl_easy_init() failed";
        return response;
    }

    std::string responseBody;

    curl_easy_reset(m_handle->curl);
    curl_easy_setopt(m_handle->curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_handle->curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(m_handle->curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(m_handle->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(m_handle->curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(m_handle->curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(m_handle->curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(m_handle->curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(m_handle->curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(m_handle->curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(m_handle->curl, CURLOPT_SSL_VERIFYHOST, 2L);

    struct curl_slist* curlHeaders = nullptr;
    for (const auto& h : headers) {
        std::string headerLine = h.name + ": " + h.value;
        curlHeaders = curl_slist_append(curlHeaders, headerLine.c_str());
    }
    if (curlHeaders) {
        curl_easy_setopt(m_handle->curl, CURLOPT_HTTPHEADER, curlHeaders);
    }

    if (method == "POST") {
        curl_easy_setopt(m_handle->curl, CURLOPT_POST, 1L);
        curl_easy_setopt(m_handle->curl, CURLOPT_POSTFIELDS, body.c_str());
    }

    CURLcode res = curl_easy_perform(m_handle->curl);
    if (res != CURLE_OK) {
        response.statusCode = 0;
        response.error = curl_easy_strerror(res);
        response.body = responseBody;
    } else {
        long httpCode = 0;
        curl_easy_getinfo(m_handle->curl, CURLINFO_RESPONSE_CODE, &httpCode);
        response.statusCode = static_cast<int>(httpCode);
        response.body = responseBody;
        if (httpCode >= 400) {
            response.error = "HTTP " + std::to_string(httpCode);
        }
    }

    if (curlHeaders) {
        curl_slist_free_all(curlHeaders);
    }

    if (res != CURLE_OK) {
        LogDebug("HTTPClient %s %s failed: curl_error=%s", method.c_str(), url.c_str(), response.error.c_str());
    } else {
        LogDebug("HTTPClient %s %s completed: status=%d, body_len=%zu", method.c_str(), url.c_str(), response.statusCode, response.body.size());
    }

    return response;
}

}
