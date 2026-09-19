#include "src/components/Factory/Factory.h"
#include "src/components/ServicesCreator/ServicesCreator.h"
#include "src/components/Console/AgentConsoleAdapter.h"
#include "src/components/Logger/Logger.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"
#include "src/tools/agent_query/AgentQueryTool.h"
#include "src/tools/agent_start/AgentStartTool.h"
#include <iostream>

using namespace Haisos;

namespace {

// Generous: a real LLM round trip, not a unit-test-scale wait.
constexpr uint64_t kAgentTimeoutMs = 5 * 60 * 1000;

bool TestAgentQuery() {
    IntegrationTest::IntegrationTestLogCapture logCapture;

    auto [endpoint, model, apiKey] = IntegrationTest::GetEndpointModelAndApiKey();

    auto factory = Factory::Create();
    auto servicesCreator = CreateServicesCreator();
    auto networkService = servicesCreator->CreateNetworkService();
    auto llmService = servicesCreator->CreateLLMService(networkService, endpoint, model, apiKey);

    auto physicalConsole = factory->CreatePhysicalConsole(false);
    physicalConsole->Start();
    auto console = AgentConsoleAdapter::Create(physicalConsole, "root");

    auto agent = llmService->CreateAgent(
        std::vector<std::string>{"You are a helpful AI assistant."},
        "root",
        nullptr,
        std::move(console),
        "",
        // Not interactive: the agent answers the prompt below and then finishes,
        // which is what this test waits for. IAgent has no Stop().
        /*interactive=*/false);

    // Create a subagent directly under the main agent
    auto subagent = Tools::CreateAndStartSubagent(
        *llmService,
        agent,
        "What is 4+4?",
        std::vector<std::string>{"You are a helpful AI assistant."},
        /*interactive=*/false);

    std::string subagentName = subagent->Name();

    // The subagent is not interactive, so it finishes on its own once it has
    // answered; there is nothing to stop.
    subagent->WaitToFinish(kAgentTimeoutMs);

    // Query its status using AgentQueryTool directly
    auto queryTool = Tools::AgentQueryTool::Create();
    nlohmann::json args;
    args["names"] = nlohmann::json::array({subagentName});
    args["return_console"] = true;
    ToolResult resultTr = queryTool->Call(agent, args);

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
