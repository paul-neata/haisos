#include "src/components/Factory/Factory.h"
#include "src/components/ToolFactory/ToolFactory.h"
#include "src/components/Logger/Logger.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include "tests/integration/helpers/IntegrationTestLogCapture.h"
#include <chrono>
#include <sstream>
#include <iomanip>

using namespace Haisos;

namespace {

bool TestToolCallIntegration() {
    IntegrationTest::IntegrationTestLogCapture logCapture;

    auto factory = CreateFactory();
    if (!factory) {
        LogError("Failed to create factory");
        return false;
    }

    auto toolFactory = factory->CreateToolFactory(*factory);

    auto tool = toolFactory->CreateTool("get_current_date_time");
    if (!tool) {
        LogError("Failed to create tool 'get_current_date_time'");
        return false;
    }

    auto testStartTime = std::chrono::system_clock::now();
    std::string result = tool->Call(nullptr, {});
    auto testEndTime = std::chrono::system_clock::now();

    if (result.empty()) {
        LogError("Tool result was empty");
        return false;
    }

    std::tm tm_buf = {};
    std::istringstream iss(result);
    iss >> std::get_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    if (iss.fail()) {
        LogError("Failed to parse tool result as date/time: %s", result.c_str());
        return false;
    }

    auto toolTime = std::chrono::system_clock::from_time_t(std::mktime(&tm_buf));
    auto elapsedBefore = std::chrono::duration_cast<std::chrono::seconds>(toolTime - testStartTime).count();
    auto elapsedAfter = std::chrono::duration_cast<std::chrono::seconds>(testEndTime - toolTime).count();

    if (elapsedBefore < -20 || elapsedAfter > 20) {
        LogError("Tool time %s is more than 20 seconds away from test time", result.c_str());
        return false;
    }

    LogInfo("Tool result: %s", result.c_str());
    return true;
}

}

int main() {
    int result = 0;

    IntegrationTest::PrintTestStart("tests/integration/RawCall.get_current_date_time.integrationtest", false);
    if (!TestToolCallIntegration()) {
        result = 1;
    }
    IntegrationTest::PrintTestEnd("tests/integration/RawCall.get_current_date_time.integrationtest", result == 0);

    return result;
}
