#include "src/components/Console/Console.h"
#include "src/components/HTTPClient/HTTPClient.h"
#include "src/components/LLMCommunicator/LLMCommunicator.h"
#include "src/components/ToolFactory/ToolFactory.h"
#include "src/components/Factory/Factory.h"
#include "src/components/ServicesCreator/ServicesCreator.h"
#include "src/components/Console/AgentConsoleAdapter.h"
#include "src/components/Logger/Logger.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"

using namespace Haisos;

namespace {

// Generous: a real LLM round trip, not a unit-test-scale wait.
constexpr uint64_t kAgentTimeoutMs = 5 * 60 * 1000;

bool TestCallGetCurrentDateTime() {
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
        "root",
        /*parent=*/nullptr,
        std::move(console),
        /*additionalTools=*/nullptr,
        std::vector<std::string>{"You are a helpful AI assistant."},
        // Not interactive: the agent answers the prompt below and then finishes,
        // which is what this test waits for. IAgent cannot be forced down.
        /*isInteractive=*/false);

    agent->Post("What is the current date and time? Please use the get_current_date_time tool to find out.");
    if (!agent->WaitToFinish(kAgentTimeoutMs)) {
        LogError("Agent did not finish within %llums", static_cast<unsigned long long>(kAgentTimeoutMs));
        logCapture.DumpIfFailed(true);
        return false;
    }

    bool success = true;
    logCapture.DumpIfFailed(!success);
    return success;
}

}

int main() {
    int result = 0;

    IntegrationTest::PrintTestStart("tests/integration/Call.get_current_date_time.integrationtest");
    if (!TestCallGetCurrentDateTime()) {
        result = 1;
    }
    IntegrationTest::PrintTestEnd("tests/integration/Call.get_current_date_time.integrationtest", result == 0);

    return result;
}
