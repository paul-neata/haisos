#include "src/components/Console/Console.h"
#include "src/components/HTTPClient/HTTPClient.h"
#include "src/components/LLMCommunicator/LLMCommunicator.h"
#include "src/components/ToolFactory/ToolFactory.h"
#include "src/components/Factory/Factory.h"
#include "src/components/Logger/Logger.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"

using namespace Haisos;

namespace {

bool TestCallGetCurrentDateTime() {
    IntegrationTest::IntegrationTestLogCapture logCapture;

    auto [endpoint, model, apiKey] = IntegrationTest::GetEndpointModelAndApiKey();

    Factory factory;
    auto physicalConsole = factory.CreatePhysicalConsole(false);
    physicalConsole->Start();
    auto console = factory.CreateAgentConsoleFromPhysical(physicalConsole, "root");
    auto httpClient = factory.CreateHTTPClient();
    auto toolFactory = factory.CreateToolFactory(factory);
    auto llmCommunicator = factory.CreateLLMCommunicator(
        std::move(httpClient), endpoint, model, apiKey);

    auto agent = factory.CreateAgent(
        std::move(llmCommunicator),
        std::move(toolFactory),
        std::move(console),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "root",
        nullptr);

    agent->Post("What is the current date and time? Please use the get_current_date_time tool to find out.");
    agent->Stop(0);
    agent->WaitToFinish();

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
