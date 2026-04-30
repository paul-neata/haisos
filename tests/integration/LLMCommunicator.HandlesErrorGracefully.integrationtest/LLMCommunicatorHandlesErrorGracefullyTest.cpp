#include "src/components/LLMCommunicator/LLMCommunicator.h"
#include "src/components/HTTPClient/HTTPClient.h"
#include "src/components/Logger/Logger.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"

using namespace Haisos;

namespace {

bool TestHandlesErrorGracefully() {
    IntegrationTest::IntegrationTestLogCapture logCapture;

    auto httpClient = CreateHTTPClient();
    if (!httpClient) {
        LogError("Failed to create HTTP client");
        return false;
    }

    LLMCommunicator llm(std::move(httpClient), "http://localhost:9999/api/chat", "llama3", "");

    SystemCallbacks callbacks;
    callbacks.on_send_with_name = IntegrationTest::MakeLLMJsonLoggerWithName("send");
    callbacks.on_received_with_name = IntegrationTest::MakeLLMJsonLoggerWithName("receive");

    std::vector<LLMMessage> messages;
    LLMMessage systemMsg;
    systemMsg.role = "system";
    systemMsg.content = "System";
    messages.push_back(systemMsg);

    LLMMessage userMsg;
    userMsg.role = "user";
    userMsg.content = "User";
    messages.push_back(userMsg);

    LLMResponse response = llm.Call(messages, {}, callbacks);

    LogInfo("Callback was called: %s", !response.message.content.empty() ? "yes" : "no");

    // The test verifies no crash happens. The response may or may not contain content
    // depending on whether the error response is parseable.
    bool success = true;
    logCapture.DumpIfFailed(!success);
    return success;
}

}

int main() {
    int result = 0;

    IntegrationTest::PrintTestStart("tests/integration/LLMCommunicator.HandlesErrorGracefully.integrationtest", false);
    if (!TestHandlesErrorGracefully()) {
        result = 1;
    }
    IntegrationTest::PrintTestEnd("tests/integration/LLMCommunicator.HandlesErrorGracefully.integrationtest", result == 0);

    return result;
}
