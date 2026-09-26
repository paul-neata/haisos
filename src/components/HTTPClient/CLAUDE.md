# HTTPClient

Platform-specific HTTP implementation.

## Responsibilities

- Provides a unified `IHTTPClient` interface
- Uses platform-specific backends:
  - **Linux**: libcurl (`linux/CurlHTTPClient`)
  - **Windows**: WinHTTP (`windows/WinHTTPClient`). WinHTTP takes UTF-16, so
    the URL and every header name and value are converted from UTF-8 with
    explicit lengths -- never `-1`, which would carry a terminating NUL into
    the string -- and a URL or header that is not UTF-8 fails the request.
    `WinHttpCrackUrl` returns only the components asked for (a null pointer
    with a length of `-1`); the request path is the URL's path plus its query
    (the "extra info"), without a fragment
  - **WASM**: emscripten_fetch (`wasm/FetchHTTPClient`)

## Usage

Call `CreateHTTPClient()` to get the appropriate implementation for the current platform.
