#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "AgentQueryTool.h"
#include "tests/mocks/MockAgent.h"

using namespace Haisos;
using namespace Haisos::Tools;
using namespace Haisos::Mocks;

TEST(AgentQueryToolTest, GetParametersSchemaIsValid) {
    auto schema = AgentQueryTool::GetDefaultParametersSchema();

    EXPECT_TRUE(schema.is_object());
    EXPECT_EQ(schema.value("type", ""), "object");
    EXPECT_TRUE(schema.contains("properties"));
    EXPECT_TRUE(schema.contains("required"));
    EXPECT_TRUE(schema["properties"].contains("names"));
}

TEST(AgentQueryToolTest, QueryOnRunningAgent) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    child->SetStartTime("2026-04-29 12:00:00");
    callerAgent->AddChild(child);

    AgentQueryTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"child1"});

    auto result = tool.Call(callerAgent, args);
    auto parsed = nlohmann::json::parse(result.content);

    ASSERT_TRUE(parsed.is_array());
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0]["name"], "child1");
    EXPECT_EQ(parsed[0]["starting_time"], "2026-04-29 12:00:00");
    EXPECT_EQ(parsed[0]["finished"], false);
    EXPECT_EQ(parsed[0]["killed"], false);
    EXPECT_FALSE(parsed[0].contains("error"));
}

TEST(AgentQueryToolTest, QueryWithReturnConsoleOnFinishedAgent) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    child->SetFinished(true);
    child->SetConsoleOutput("console output");
    callerAgent->AddChild(child);

    AgentQueryTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"child1"});
    args["return_console"] = true;

    auto result = tool.Call(callerAgent, args);
    auto parsed = nlohmann::json::parse(result.content);

    ASSERT_TRUE(parsed.is_array());
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0]["name"], "child1");
    EXPECT_TRUE(parsed[0].contains("console_result"));
    EXPECT_FALSE(parsed[0].contains("error"));
}

TEST(AgentQueryToolTest, QueryWithReturnMessages) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    nlohmann::json history = nlohmann::json::array({{{"role", "user"}, {"content", "Hello"}}});
    child->SetHistory(history);
    callerAgent->AddChild(child);

    AgentQueryTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"child1"});
    args["return_messages"] = true;

    auto result = tool.Call(callerAgent, args);
    auto parsed = nlohmann::json::parse(result.content);

    ASSERT_TRUE(parsed.is_array());
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0]["name"], "child1");
    EXPECT_TRUE(parsed[0].contains("messages_result"));
    EXPECT_TRUE(parsed[0]["messages_result"].is_array());
    EXPECT_FALSE(parsed[0].contains("error"));
}

TEST(AgentQueryToolTest, QueryOnNonExistentAgent) {
    auto callerAgent = std::make_shared<MockAgent>();
    AgentQueryTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"nonexistent"});

    auto result = tool.Call(callerAgent, args);
    EXPECT_TRUE(result.isError);
    auto parsed = nlohmann::json::parse(result.content);

    ASSERT_TRUE(parsed.is_array());
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0]["name"], "nonexistent");
    EXPECT_EQ(parsed[0]["found"], false);
    EXPECT_FALSE(parsed[0].contains("error"));
}

TEST(AgentQueryToolTest, QueryMixedFoundAndNotFound) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    child->SetStartTime("2026-04-29 12:00:00");
    callerAgent->AddChild(child);

    AgentQueryTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"child1", "missing"});

    auto result = tool.Call(callerAgent, args);
    auto parsed = nlohmann::json::parse(result.content);

    ASSERT_TRUE(parsed.is_array());
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0]["name"], "child1");
    EXPECT_TRUE(parsed[0].contains("starting_time"));
    EXPECT_EQ(parsed[1]["name"], "missing");
    EXPECT_EQ(parsed[1]["found"], false);
    EXPECT_FALSE(parsed[1].contains("error"));
}

TEST(AgentQueryToolTest, QueryMissingNamesReturnsError) {
    auto callerAgent = std::make_shared<MockAgent>();
    AgentQueryTool tool;

    nlohmann::json args;
    // no "names" field

    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.isError);
    EXPECT_EQ(result.content, "Missing required field: names");
}
