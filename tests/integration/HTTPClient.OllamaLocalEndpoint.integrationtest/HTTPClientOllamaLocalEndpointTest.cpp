#include "src/components/HTTPClient/HTTPClient.h"
#include "src/components/Logger/Logger.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace Haisos;

namespace {

bool TestOllamaLocalEndpoint() {
    IntegrationTest::IntegrationTestLogCapture logCapture;

    auto client = CreateHTTPClient();
    if (!client) {
        LogError("Failed to create HTTP client");
        return false;
    }

    auto [endpoint, model, apiKey] = IntegrationTest::GetEndpointModelAndApiKey();
    (void)apiKey;

    LogInfo("HTTP POST: %s (model=%s)", endpoint.c_str(), model.c_str());

    json requestBody;
    requestBody["model"] = model;
    requestBody["messages"] = json::array({{{"role", "user"}, {"content", "Hello"}}});
    requestBody["stream"] = false;

    HTTPResponse response = client->Post(endpoint, requestBody.dump());

    LogInfo("Ollama response: %s", response.body.c_str());

    if (!response.IsSuccess()) {
        LogError("Ollama POST request failed: %s", response.error.c_str());
        return false;
    }

    if (response.body.empty()) {
        LogError("Ollama POST response body was empty");
        return false;
    }

    bool success = true;
    logCapture.DumpIfFailed(!success);
    return success;
}

}

int main() {
    int result = 0;

    IntegrationTest::PrintTestStart("tests/integration/HTTPClient.OllamaLocalEndpoint.integrationtest");
    if (!TestOllamaLocalEndpoint()) {
        result = 1;
    }
    IntegrationTest::PrintTestEnd("tests/integration/HTTPClient.OllamaLocalEndpoint.integrationtest", result == 0);

    return result;
}
