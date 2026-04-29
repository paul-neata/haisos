#include "src/components/LLMCommunicator/LLMCommunicator.h"
#include "src/components/HTTPClient/HTTPClient.h"
#include "src/components/Logger/Logger.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"
#include <thread>
#include <chrono>

using namespace Haisos;

namespace {

bool TestRunWithSimplePrompt() {
    IntegrationTest::IntegrationTestLogCapture logCapture;

    auto [endpoint, model, apiKey] = IntegrationTest::GetEndpointModelAndApiKey();

    LogInfo("RunWithSimplePrompt using endpoint=%s model=%s", endpoint.c_str(), model.c_str());

    auto httpClient = CreateHTTPClient();
    if (!httpClient) {
        LogError("Failed to create HTTP client");
        return false;
    }

    LLMCommunicator llm(std::move(httpClient), endpoint, model, apiKey);

    std::string responseReceived;

    SystemCallbacks callbacks;
    callbacks.on_send = IntegrationTest::MakeLLMJsonLogger("send", "");
    callbacks.on_received = IntegrationTest::MakeLLMJsonLogger("receive", "");

    std::vector<LLMMessage> messages;
    LLMMessage systemMsg;
    systemMsg.role = "system";
    systemMsg.content = "You are a helpful assistant.";
    messages.push_back(systemMsg);

    LLMMessage userMsg;
    userMsg.role = "user";
    userMsg.content = "Hello, say hello back.";
    messages.push_back(userMsg);

    LLMResponse response = llm.Call(messages, {}, callbacks);
    responseReceived = response.message.content;

    LogInfo("Response received: %s", responseReceived.c_str());

    if (responseReceived.empty()) {
        LogError("No response received from LLM");
        return false;
    }

    bool success = true;
    logCapture.DumpIfFailed(!success);
    return success;
}

}

int main() {
    int result = 0;

    IntegrationTest::PrintTestStart("tests/integration/RunWithSimplePrompt.integrationtest");
    if (!TestRunWithSimplePrompt()) {
        result = 1;
    }
    IntegrationTest::PrintTestEnd("tests/integration/RunWithSimplePrompt.integrationtest", result == 0);

    return result;
}
