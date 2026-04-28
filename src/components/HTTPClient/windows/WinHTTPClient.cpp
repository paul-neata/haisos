#include "WinHTTPClient.h"
#include <Windows.h>
#include <winhttp.h>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace Haisos {

#ifdef _WIN32

struct WinHTTPClient::WinHTTPHandle {
    HINTERNET session;
    WinHTTPHandle() : session(nullptr) {}
    ~WinHTTPHandle() { if (session) WinHttpCloseHandle(session); }
};

WinHTTPClient::WinHTTPClient() : m_handle(std::make_unique<WinHTTPHandle>()) {
    m_handle->session = WinHttpOpen(
        L"Haisos/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
}

WinHTTPClient::~WinHTTPClient() = default;

HTTPResponse WinHTTPClient::Get(const std::string& url) {
    return PerformRequest(url, L"GET");
}

HTTPResponse WinHTTPClient::Post(const std::string& url, const std::string& body) {
    return PerformRequest(url, L"POST", body, {});
}

HTTPResponse WinHTTPClient::Post(const std::string& url, const std::string& body, const std::vector<HTTPHeader>& headers) {
    return PerformRequest(url, L"POST", body, headers);
}

HTTPResponse WinHTTPClient::PerformRequest(const std::string& url, const wchar_t* method, const std::string& body, const std::vector<HTTPHeader>& headers) {
    HTTPResponse response;

    if (!m_handle->session) {
        response.error = "Error: Failed to initialize WinHTTP";
        return response;
    }

    // Parse URL
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.lpszHostName = nullptr;
    urlComp.dwHostNameLength = 0;
    urlComp.lpszUrlPath = nullptr;
    urlComp.dwUrlPathLength = 0;

    // Convert URL to wide string
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wideUrl(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wideUrl[0], wideLen);

    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &urlComp)) {
        response.error = "Error: Failed to parse URL";
        return response;
    }

    std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

    // Connect
    HINTERNET connect = WinHttpConnect(m_handle->session, hostName.c_str(), urlComp.nPort, 0);
    if (!connect) {
        response.error = "Error: Failed to connect";
        return response;
    }

    // Open request
    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connect,
        method,
        urlPath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        flags);

    if (!request) {
        WinHttpCloseHandle(connect);
        response.error = "Error: Failed to open request";
        return response;
    }

    // Set timeout: 120s total, 30s connect
    DWORD timeout = 120000;
    WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    timeout = 30000;
    WinHttpSetOption(request, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));

    // Build headers
    std::wstring extraHeaders;
    for (const auto& h : headers) {
        int nameLen = MultiByteToWideChar(CP_UTF8, 0, h.name.c_str(), -1, nullptr, 0);
        int valueLen = MultiByteToWideChar(CP_UTF8, 0, h.value.c_str(), -1, nullptr, 0);
        std::wstring wname(nameLen, L'\0');
        std::wstring wvalue(valueLen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, h.name.c_str(), -1, &wname[0], nameLen);
        MultiByteToWideChar(CP_UTF8, 0, h.value.c_str(), -1, &wvalue[0], valueLen);
        extraHeaders += wname + L": " + wvalue + L"\r\n";
    }

    // Send request
    BOOL result;
    if (wcscmp(method, L"POST") == 0 && !body.empty()) {
        result = WinHttpSendRequest(
            request,
            extraHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extraHeaders.c_str(),
            static_cast<DWORD>(extraHeaders.size()),
            const_cast<char*>(body.c_str()),
            static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()),
            0);
    } else {
        result = WinHttpSendRequest(
            request,
            extraHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extraHeaders.c_str(),
            static_cast<DWORD>(extraHeaders.size()),
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0);
    }

    if (!result) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        response.error = "Error: Failed to send request";
        return response;
    }

    // Receive response
    std::string responseData;
    DWORD bytesRead = 0;
    char buffer[4096];

    result = WinHttpReceiveResponse(request, nullptr);
    if (result) {
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
        response.statusCode = static_cast<int>(statusCode);
        if (statusCode >= 400) {
            response.error = "HTTP " + std::to_string(statusCode);
        }
        while (true) {
            result = WinHttpReadData(request, buffer, sizeof(buffer), &bytesRead);
            if (!result || bytesRead == 0) break;
            responseData.append(buffer, bytesRead);
        }
    } else {
        response.error = "Error: Failed to receive response";
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);

    response.body = responseData;
    return response;
}

#endif
}
