#pragma once
#include "interfaces/IHTTPClient.h"
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
inline std::unique_ptr<IHTTPClient> CreateHTTPClient() {
#if defined(PLATFORM_WINDOWS)
    return std::make_unique<WinHTTPClient>();
#elif defined(PLATFORM_WASM)
    return std::make_unique<FetchHTTPClient>();
#else
    return std::make_unique<CurlHTTPClient>();
#endif
}

}