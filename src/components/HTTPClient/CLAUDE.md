# HTTPClient

Platform-specific HTTP implementation.

## Responsibilities

- Provides a unified `IHTTPClient` interface
- Uses platform-specific backends:
  - **Linux**: libcurl (`linux/CurlHTTPClient`)
  - **Windows**: WinHTTP (`windows/WinHTTPClient`)
  - **WASM**: emscripten_fetch (`wasm/FetchHTTPClient`)

## Usage

Call `CreateHTTPClient()` to get the appropriate implementation for the current platform.
