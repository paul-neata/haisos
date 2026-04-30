#include "src/components/Factory/Factory.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"

using namespace Haisos;

namespace {

bool TestAgentWaitToFinish() {
    IntegrationTest::IntegrationTestLogCapture logCapture;

    auto [endpoint, model, apiKey] = IntegrationTest::GetEndpointModelAndApiKey();

    Factory factory;
    auto console = factory.CreateConsole(false);
    console->Start();
    auto httpClient = factory.CreateHTTPClient();
    auto toolFactory = factory.CreateToolFactory(factory);
    auto llmCommunicator = factory.CreateLLMCommunicator(
        std::move(httpClient), endpoint, model, apiKey);

    SystemCallbacks callbacks;
    callbacks.on_send_with_name = IntegrationTest::MakeLLMJsonLoggerWithName("send");
    callbacks.on_received_with_name = IntegrationTest::MakeLLMJsonLoggerWithName("receive");
    factory.SetSystemCallbacks(callbacks);

    auto agent = factory.CreateAgent(
        std::move(llmCommunicator),
        std::move(toolFactory),
        std::move(console),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "root",
        nullptr);

    agent->Post("Start a subagent with prompt 'What is 3+3?', then wait for it to finish with timeout 30000.");
    agent->Stop(0);
    agent->WaitToFinish();

    bool success = true;
    logCapture.DumpIfFailed(!success);
    return success;
}

}

int main() {
    int result = 0;

    IntegrationTest::PrintTestStart("tests/integration/AgentWaitToFinish.integrationtest");
    if (!TestAgentWaitToFinish()) {
        result = 1;
    }
    IntegrationTest::PrintTestEnd("tests/integration/AgentWaitToFinish.integrationtest", result == 0);

    return result;
}
