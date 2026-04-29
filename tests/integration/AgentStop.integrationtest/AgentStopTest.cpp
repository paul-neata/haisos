#include "src/components/Factory/Factory.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"

using namespace Haisos;

namespace {

bool TestAgentStop() {
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
    callbacks.on_send = IntegrationTest::MakeLLMJsonLogger("send", "integration_agent");
    callbacks.on_received = IntegrationTest::MakeLLMJsonLogger("receive", "integration_agent");
    factory.SetSystemCallbacks(callbacks);

    auto agent = factory.CreateAgent(
        std::move(llmCommunicator),
        std::move(toolFactory),
        std::move(console),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "integration_agent",
        nullptr);

    agent->Post("Start a subagent with prompt 'What is 1+1?' using agent_start without wait. Then use agent_stop to stop it. Return the result.");
    agent->Stop(0);
    agent->WaitToFinish();

    bool success = true;
    logCapture.DumpIfFailed(!success);
    return success;
}

}

int main() {
    int result = 0;

    IntegrationTest::PrintTestStart("tests/integration/AgentStop.integrationtest");
    if (!TestAgentStop()) {
        result = 1;
    }
    IntegrationTest::PrintTestEnd("tests/integration/AgentStop.integrationtest", result == 0);

    return result;
}
