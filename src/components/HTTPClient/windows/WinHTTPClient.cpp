#include "WinHTTPClient.h"
#include <Windows.h>
#include <winhttp.h>
#include <climits>
#include <string>
#include <utility>

#pragma comment(lib, "winhttp.lib")

namespace Haisos {

#ifdef _WIN32

namespace {

// Converts UTF-8 |text| to the UTF-16 WinHTTP takes. The lengths are explicit
// on both sides, so |out| holds exactly the characters of |text| and no
// terminating NUL -- one converted with a length of -1 keeps its NUL, which
// then sits inside the URL or the header it is pasted into. Returns false if
// |text| is not valid UTF-8 or too long to convert; an empty |text| converts
// to an empty |out| (MultiByteToWideChar itself refuses a length of 0).
bool Utf8ToWide(const std::string& text, std::wstring& out) {
    out.clear();
    if (text.empty()) {
        return true;
    }
    if (text.size() > static_cast<size_t>(INT_MAX)) {
        return false;
    }
    const int length = static_cast<int>(text.size());
    const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (wideLength <= 0) {
        return false;
    }
    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, &wide[0], wideLength) != wideLength) {
        return false;
    }
    out = std::move(wide);
    return true;
}

// A component WinHttpCrackUrl found: it points into the URL it was given,
// |length| characters long and not NUL-terminated. One the URL does not have
// comes back empty, possibly as a null pointer.
std::wstring UrlComponent(const wchar_t* start, DWORD length) {
    if (start == nullptr || length == 0) {
        return std::wstring();
    }
    return std::wstring(start, static_cast<size_t>(length));
}

} // namespace

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
    std::wstring wideUrl;
    if (!Utf8ToWide(url, wideUrl) || wideUrl.empty()) {
        response.error = "Error: Failed to parse URL";
        return response;
    }

    // WinHttpCrackUrl returns only the components asked for: a null pointer
    // with a nonzero length asks it to point into wideUrl for that component,
    // while a null pointer with a zero length means "not wanted" -- which is
    // how the host name once came back empty and no server could be reached.
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.lpszScheme = nullptr;
    urlComp.dwSchemeLength = static_cast<DWORD>(-1);
    urlComp.lpszHostName = nullptr;
    urlComp.dwHostNameLength = static_cast<DWORD>(-1);
    urlComp.lpszUrlPath = nullptr;
    urlComp.dwUrlPathLength = static_cast<DWORD>(-1);
    urlComp.lpszExtraInfo = nullptr;
    urlComp.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &urlComp)) {
        response.error = "Error: Failed to parse URL";
        return response;
    }

    // Copied out while wideUrl, which the components point into, is alive.
    const std::wstring hostName = UrlComponent(urlComp.lpszHostName, urlComp.dwHostNameLength);
    if (hostName.empty()) {
        response.error = "Error: Failed to parse URL";
        return response;
    }
    // What is requested is the path plus the query ("?..."), which
    // WinHttpCrackUrl hands back apart, as the extra info; a fragment ("#...")
    // is the client's own business and is never sent.
    std::wstring urlPath = UrlComponent(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlPath.empty()) {
        urlPath = L"/";
    }
    std::wstring extraInfo = UrlComponent(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    const size_t fragment = extraInfo.find(L'#');
    if (fragment != std::wstring::npos) {
        extraInfo.resize(fragment);
    }
    urlPath += extraInfo;

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

    // Build headers. Each name and value is converted without a terminating
    // NUL: one left in would split "Content-Type\0: application/json" and
    // break every header, Authorization included.
    std::wstring extraHeaders;
    for (const auto& h : headers) {
        std::wstring wname;
        std::wstring wvalue;
        if (!Utf8ToWide(h.name, wname) || !Utf8ToWide(h.value, wvalue)) {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            // The name only: the value may be a secret, such as an API key.
            response.error = "Error: HTTP header " + h.name + " is not valid UTF-8";
            return response;
        }
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
