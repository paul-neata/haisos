#pragma once
#include <string>
#include "interfaces/IHTTPClient.h"

namespace Haisos::Mocks {

class MockHTTPClient : public IHTTPClient {
public:
    MockHTTPClient() : m_callCount(0) {}

    void SetGetResponse(const std::string& response) { m_getResponse = HTTPResponse{200, response, ""}; }
    void SetPostResponse(const std::string& response) { m_postResponse = HTTPResponse{200, response, ""}; }

    HTTPResponse Get(const std::string& url) override {
        m_lastUrl = url;
        m_lastMethod = "GET";
        m_lastBody = "";
        ++m_callCount;
        return m_getResponse;
    }

    HTTPResponse Post(const std::string& url, const std::string& body) override {
        m_lastUrl = url;
        m_lastMethod = "POST";
        m_lastBody = body;
        ++m_callCount;
        return m_postResponse;
    }

    HTTPResponse Post(const std::string& url, const std::string& body, const std::vector<HTTPHeader>& headers) override {
        m_lastUrl = url;
        m_lastMethod = "POST";
        m_lastBody = body;
        m_lastHeaders = headers;
        ++m_callCount;
        return m_postResponse;
    }

    const std::string& GetLastUrl() const { return m_lastUrl; }
    const std::string& GetLastMethod() const { return m_lastMethod; }
    const std::string& GetLastBody() const { return m_lastBody; }
    int GetCallCount() const { return m_callCount; }

private:
    HTTPResponse m_getResponse;
    HTTPResponse m_postResponse;
    std::string m_lastUrl;
    std::string m_lastMethod;
    std::string m_lastBody;
    std::vector<HTTPHeader> m_lastHeaders;
    int m_callCount;
};

}
