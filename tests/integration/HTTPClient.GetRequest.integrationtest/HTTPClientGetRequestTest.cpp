#include "src/components/HTTPClient/HTTPClient.h"
#include "src/components/Logger/Logger.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"

using namespace Haisos;

namespace {

bool TestGetRequest() {
    IntegrationTest::IntegrationTestLogCapture logCapture;

    auto client = CreateHTTPClient();
    if (!client) {
        LogError("Failed to create HTTP client");
        return false;
    }

    LogInfo("HTTP GET: https://jsonplaceholder.typicode.com/posts/1");
    HTTPResponse response = client->Get("https://jsonplaceholder.typicode.com/posts/1");

    if (!response.IsSuccess()) {
        LogError("GET request failed: %s", response.error.c_str());
        return false;
    }

    if (response.body.empty()) {
        LogError("GET response body was empty");
        return false;
    }

    if (response.body.find("userId") == std::string::npos) {
        LogError("GET response did not contain expected content: %s", response.body.c_str());
        return false;
    }

    bool success = true;
    logCapture.DumpIfFailed(!success);
    return success;
}

}

int main() {
    int result = 0;

    IntegrationTest::PrintTestStart("tests/integration/HTTPClient.GetRequest.integrationtest", false);
    if (!TestGetRequest()) {
        result = 1;
    }
    IntegrationTest::PrintTestEnd("tests/integration/HTTPClient.GetRequest.integrationtest", result == 0);

    return result;
}
