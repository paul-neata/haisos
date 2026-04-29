#pragma once
#include <string>
#include <vector>

namespace Haisos {

struct HTTPHeader {
    std::string name;
    std::string value;
};

struct HTTPResponse {
    int statusCode = 0;
    std::string body;
    std::string error;
    bool IsSuccess() const { return statusCode >= 200 && statusCode < 300 && error.empty(); }
};

class IHTTPClient {
public:
    virtual ~IHTTPClient() = default;
    virtual HTTPResponse Get(const std::string& url) = 0;
    virtual HTTPResponse Post(const std::string& url, const std::string& body) = 0;
    virtual HTTPResponse Post(const std::string& url, const std::string& body, const std::vector<HTTPHeader>& headers) = 0;
};

}
