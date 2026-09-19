#pragma once
#include "interfaces/INetworkService.h"
#include <memory>

// Platform detection
#if defined(__linux__)
    #include "linux/CurlHTTPClient.h"
    #define PLATFORM_LINUX 1
#elif defined(_WIN32)
    #include "windows/WinHTTPClient.h"
    #define PLATFORM_WINDOWS 1
#elif defined(__EMSCRIPTEN__)
    #include "wasm/FetchHTTPClient.h"
    #define PLATFORM_WASM 1
#endif

namespace Haisos {

// Create the appropriate HTTP client based on platform
inline std::shared_ptr<IHTTPClient> CreateHTTPClient() {
#if defined(PLATFORM_WINDOWS)
    return WinHTTPClient::Create();
#elif defined(PLATFORM_WASM)
    return FetchHTTPClient::Create();
#else
    return CurlHTTPClient::Create();
#endif
}

}