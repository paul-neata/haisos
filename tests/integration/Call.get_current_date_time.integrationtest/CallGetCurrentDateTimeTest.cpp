#include "src/components/HaisosEngine/HaisosEngine.h"
#include "src/components/Console/Console.h"
#include "src/components/HTTPClient/HTTPClient.h"
#include "src/components/LLMCommunicator/LLMCommunicator.h"
#include "src/components/ToolFactory/ToolFactory.h"
#include "src/components/Factory/Factory.h"
#include "src/components/Logger/Logger.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"
#include <fstream>

using namespace Haisos;

namespace {

bool TestCallGetCurrentDateTimeViaEngine() {
    IntegrationTest::IntegrationTestLogCapture logCapture;

    auto [endpoint, model, apiKey] = IntegrationTest::GetEndpointModelAndApiKey();

    Factory factory;
    auto engine = factory.CreateHaisosEngine(factory);

    std::string testFile = "test_get_time.md";
    {
        std::ofstream outFile(testFile);
        outFile << "What is the current date and time? Please use the get_current_date_time tool to find out.";
        outFile.close();
    }

    SystemCallbacks callbacks;
    callbacks.on_send = IntegrationTest::MakeLLMJsonLogger("send", "");
    callbacks.on_received = IntegrationTest::MakeLLMJsonLogger("receive", "");

    RunConfig config;
    config.userPrompt = testFile;
    config.useFile = true;
    engine->Run(config, callbacks);

    std::remove(testFile.c_str());

    bool success = true;
    logCapture.DumpIfFailed(!success);
    return success;
}

}

int main() {
    int result = 0;

    IntegrationTest::PrintTestStart("tests/integration/Call.get_current_date_time.integrationtest");
    if (!TestCallGetCurrentDateTimeViaEngine()) {
        result = 1;
    }
    IntegrationTest::PrintTestEnd("tests/integration/Call.get_current_date_time.integrationtest", result == 0);

    return result;
}
