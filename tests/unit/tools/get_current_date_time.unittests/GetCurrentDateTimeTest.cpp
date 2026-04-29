#include <gtest/gtest.h>
#include "GetCurrentDateTime.h"
#include <regex>
#include <chrono>
#include <ctime>

using namespace Haisos;
using namespace Haisos::Tools;

TEST(GetCurrentDateTimeTest, CallReturnsNonEmptyString) {
    GetCurrentDateTime tool;
    auto result = tool.Call(nullptr, {});

    EXPECT_FALSE(result.content.empty());
    EXPECT_FALSE(result.isError);
}

TEST(GetCurrentDateTimeTest, CallReturnsLocalTimeByDefault) {
    GetCurrentDateTime tool;
    auto result = tool.Call(nullptr, {});

    EXPECT_FALSE(result.isError);
    std::string content = result.content;

    // Format should be: YYYY-MM-DD HH:MM:SS
    std::regex pattern(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})");
    EXPECT_TRUE(std::regex_match(content, pattern)) << "Result: " << content;
}

TEST(GetCurrentDateTimeTest, CallReturnsLocalTimeWhenGetGmtIsFalse) {
    GetCurrentDateTime tool;
    nlohmann::json args;
    args["get_gmt"] = false;
    auto result = tool.Call(nullptr, args);

    EXPECT_FALSE(result.isError);
    std::string content = result.content;

    // Format should be: YYYY-MM-DD HH:MM:SS
    std::regex pattern(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})");
    EXPECT_TRUE(std::regex_match(content, pattern)) << "Result: " << content;
}

TEST(GetCurrentDateTimeTest, CallReturnsGMTTimeWhenGetGmtIsTrue) {
    GetCurrentDateTime tool;
    nlohmann::json args;
    args["get_gmt"] = true;
    auto result = tool.Call(nullptr, args);

    EXPECT_FALSE(result.isError);
    std::string content = result.content;

    // Format should be: YYYY-MM-DDTHH:MM:SSZ
    std::regex pattern(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)");
    EXPECT_TRUE(std::regex_match(content, pattern)) << "Result: " << content;
}

TEST(GetCurrentDateTimeTest, CallReturnsIsErrorFalse) {
    GetCurrentDateTime tool;
    auto result = tool.Call(nullptr, {});

    EXPECT_FALSE(result.isError);
}

TEST(GetCurrentDateTimeTest, CallReturnsCurrentLocalTime) {
    GetCurrentDateTime tool;
    auto result = tool.Call(nullptr, {});

    std::string content = result.content;

    // Get current time to compare
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:", &tm);
    std::string expectedPrefix(buffer);

    // Result should start with today's date and current hour
    EXPECT_EQ(content.substr(0, expectedPrefix.length()), expectedPrefix)
        << "Expected prefix: " << expectedPrefix << ", got: " << content;
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

TEST(GetCurrentDateTimeTest, GetParametersSchemaContainsGetGmt) {
    GetCurrentDateTime tool;
    auto schema = tool.GetParametersSchema();

    ASSERT_TRUE(schema.contains("properties"));
    auto properties = schema["properties"];
    EXPECT_TRUE(properties.contains("get_gmt"));
    EXPECT_EQ(properties["get_gmt"]["type"], "boolean");
}
