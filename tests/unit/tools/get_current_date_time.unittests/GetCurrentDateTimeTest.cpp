#include <gtest/gtest.h>
#include "GetCurrentDateTime.h"
#include <regex>
#include <chrono>
#include <ctime>

using namespace Haisos::Tools;

TEST(GetCurrentDateTimeTest, CallReturnsNonEmptyString) {
    GetCurrentDateTime tool;
    std::string result = tool.Call(nullptr, {});

    EXPECT_FALSE(result.empty());
}

TEST(GetCurrentDateTimeTest, CallReturnsValidDateFormat) {
    GetCurrentDateTime tool;
    std::string result = tool.Call(nullptr, {});

    // Format should be: YYYY-MM-DD HH:MM:SS
    // Regex pattern for date and time
    std::regex pattern(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})");
    EXPECT_TRUE(std::regex_match(result, pattern)) << "Result: " << result;
}

TEST(GetCurrentDateTimeTest, ToolNameIsCorrect) {
    EXPECT_EQ(GetCurrentDateTime::ToolName, "get_current_date_time");
}

TEST(GetCurrentDateTimeTest, ToolDefaultDescriptionIsSet) {
    EXPECT_FALSE(GetCurrentDateTime::ToolDefaultDescription.empty());
}

TEST(GetCurrentDateTimeTest, GetParametersSchemaIsValid) {
    GetCurrentDateTime tool;
    auto schema = tool.GetParametersSchema();

    EXPECT_TRUE(schema.is_object());
    EXPECT_EQ(schema.value("type", ""), "object");
    EXPECT_TRUE(schema.contains("properties"));
}

TEST(GetCurrentDateTimeTest, CallReturnsCurrentTime) {
    GetCurrentDateTime tool;
    std::string result = tool.Call(nullptr, {});

    // Get current time to compare
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:", &tm);
    std::string expectedPrefix(buffer);

    // Result should start with today's date and current hour
    EXPECT_EQ(result.substr(0, expectedPrefix.length()), expectedPrefix)
        << "Expected prefix: " << expectedPrefix << ", got: " << result;
}
