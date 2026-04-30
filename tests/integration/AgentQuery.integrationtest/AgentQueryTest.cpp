#include "src/components/Factory/Factory.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"
#include "src/tools/agent_query/AgentQueryTool.h"
#include "src/tools/agent_start/AgentStartTool.h"
#include <iostream>

using namespace Haisos;

namespace {

bool TestAgentQuery() {
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

    // Create a subagent directly under the main agent
    auto subagent = Tools::CreateAndStartSubagent(
        factory,
        agent,
        "What is 4+4?",
        std::vector<std::string>{"You are a helpful AI assistant."});

    std::string subagentName = subagent->Name();

    // Close the queue and wait for the subagent to finish
    subagent->Stop(0);
    subagent->WaitToFinish();

    // Query its status using AgentQueryTool directly
    Tools::AgentQueryTool queryTool;
    nlohmann::json args;
    args["names"] = nlohmann::json::array({subagentName});
    args["return_console"] = true;
    ToolResult resultTr = queryTool.Call(agent, args);

    // Parse and verify the result (agent_query returns raw JSON array on success)
    nlohmann::json result = nlohmann::json::parse(resultTr.content);
    bool success = true;

    if (!result.is_array() || result.empty()) {
        std::cerr << "AgentQueryTest: expected non-empty result array\n";
        success = false;
    } else {
        const auto& entry = result[0];
        if (entry.value("name", "") != subagentName) {
            std::cerr << "AgentQueryTest: expected name '" << subagentName
                      << "', got '" << entry.value("name", "") << "'\n";
            success = false;
        }
        if (!entry.value("finished", false)) {
            std::cerr << "AgentQueryTest: expected finished=true\n";
            success = false;
        }
        if (!entry.contains("console_result") || entry["console_result"].get<std::string>().empty()) {
            std::cerr << "AgentQueryTest: expected non-empty console_result\n";
            success = false;
        }
    }

    logCapture.DumpIfFailed(!success);
    return success;
}

}

int main() {
    int result = 0;

    IntegrationTest::PrintTestStart("tests/integration/AgentQuery.integrationtest");
    if (!TestAgentQuery()) {
        result = 1;
    }
    IntegrationTest::PrintTestEnd("tests/integration/AgentQuery.integrationtest", result == 0);

    return result;
}
