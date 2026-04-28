#include "src/components/LLMCommunicator/LLMCommunicator.h"
#include "src/components/HTTPClient/HTTPClient.h"
#include "src/components/Logger/Logger.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"
#include <thread>
#include <chrono>

using namespace Haisos;

namespace {

bool TestCallLocalOllama() {
    IntegrationTest::IntegrationTestLogCapture logCapture;

    auto [endpoint, model, apiKey] = IntegrationTest::GetEndpointModelAndApiKey();

    auto httpClient = CreateHTTPClient();
    if (!httpClient) {
        LogError("Failed to create HTTP client");
        return false;
    }

    LLMCommunicator llm(std::move(httpClient), endpoint, model, apiKey);

    std::string responseReceived;
    std::string toolCalled;
    std::string toolInput;

    JsonSendReceiveCallbacks callbacks;
    callbacks.on_send = IntegrationTest::MakeLLMJsonLogger("send");
    callbacks.on_received = IntegrationTest::MakeLLMJsonLogger("receive");

    std::vector<LLMMessage> messages;
    LLMMessage systemMsg;
    systemMsg.role = "system";
    systemMsg.content = "You are a helpful assistant.";
    messages.push_back(systemMsg);

    LLMMessage userMsg;
    userMsg.role = "user";
    userMsg.content = "What is the mathematical sum of 2 and 3 ?";
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

    IntegrationTest::PrintTestStart("tests/integration/LLMCommunicator.2Plus3.integrationtest");
    if (!TestCallLocalOllama()) {
        result = 1;
    }
    IntegrationTest::PrintTestEnd("tests/integration/LLMCommunicator.2Plus3.integrationtest", result == 0);

    return result;
}
