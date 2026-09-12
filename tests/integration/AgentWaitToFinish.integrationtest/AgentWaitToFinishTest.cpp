#include "src/components/Factory/Factory.h"
#include "src/components/ServicesCreator/ServicesCreator.h"
#include "src/components/Console/AgentConsoleAdapter.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"

using namespace Haisos;

namespace {

bool TestAgentWaitToFinish() {
    IntegrationTest::IntegrationTestLogCapture logCapture;

    auto [endpoint, model, apiKey] = IntegrationTest::GetEndpointModelAndApiKey();

    Factory factory;
    auto servicesCreator = CreateServicesCreator(factory);
    auto networkService = std::shared_ptr<INetworkService>(servicesCreator->CreateNetworkService());
    auto llmService = servicesCreator->CreateLLMService(*networkService, endpoint, model, apiKey);

    auto physicalConsole = factory.CreatePhysicalConsole(false);
    physicalConsole->Start();
    auto console = std::make_unique<AgentConsoleAdapter>(physicalConsole, "root");

    auto agent = llmService->CreateAgent(
        std::vector<std::string>{"You are a helpful AI assistant."},
        "root",
        nullptr,
        std::move(console));

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
