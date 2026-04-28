add_library(HTTPClient STATIC)

target_include_directories(HTTPClient PUBLIC
    ${CMAKE_SOURCE_DIR}/interfaces
    ${CMAKE_SOURCE_DIR}/src/components/HTTPClient
)

target_compile_features(HTTPClient PUBLIC cxx_std_17)

if(WIN32)
    target_sources(HTTPClient PRIVATE windows/WinHTTPClient.cpp)
elseif(EMSCRIPTEN)
    target_sources(HTTPClient PRIVATE wasm/FetchHTTPClient.cpp)
else()
    target_sources(HTTPClient PRIVATE linux/CurlHTTPClient.cpp)
    target_link_libraries(HTTPClient PUBLIC CURL::libcurl)
endif()
