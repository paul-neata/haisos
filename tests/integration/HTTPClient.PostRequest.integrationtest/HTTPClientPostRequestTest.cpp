#include "src/components/HTTPClient/HTTPClient.h"
#include "src/components/Logger/Logger.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"

using namespace Haisos;

namespace {

bool TestPostRequest() {
    IntegrationTest::IntegrationTestLogCapture logCapture;

    auto client = CreateHTTPClient();
    if (!client) {
        LogError("Failed to create HTTP client");
        return false;
    }

    std::string body = R"({"title":"test","body":"test body","userId":1})";
    LogInfo("HTTP POST: https://jsonplaceholder.typicode.com/posts");
    HTTPResponse response = client->Post("https://jsonplaceholder.typicode.com/posts", body);

    if (!response.IsSuccess()) {
        LogError("POST request failed: %s", response.error.c_str());
        return false;
    }

    if (response.body.empty()) {
        LogError("POST response body was empty");
        return false;
    }

    bool success = true;
    logCapture.DumpIfFailed(!success);
    return success;
}

}

int main() {
    int result = 0;

    IntegrationTest::PrintTestStart("tests/integration/HTTPClient.PostRequest.integrationtest", false);
    if (!TestPostRequest()) {
        result = 1;
    }
    IntegrationTest::PrintTestEnd("tests/integration/HTTPClient.PostRequest.integrationtest", result == 0);

    return result;
}
