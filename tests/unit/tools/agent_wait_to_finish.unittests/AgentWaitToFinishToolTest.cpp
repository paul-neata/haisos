#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "AgentWaitToFinishTool.h"
#include "tests/mocks/MockAgent.h"

using namespace Haisos;
using namespace Haisos::Tools;
using namespace Haisos::Mocks;

TEST(AgentWaitToFinishToolTest, GetParametersSchemaIsValid) {
    auto schema = AgentWaitToFinishTool::GetDefaultParametersSchema();

    EXPECT_TRUE(schema.is_object());
    EXPECT_EQ(schema.value("type", ""), "object");
    EXPECT_TRUE(schema.contains("properties"));
    EXPECT_TRUE(schema.contains("required"));
    EXPECT_TRUE(schema["properties"].contains("names"));
    EXPECT_TRUE(schema["properties"].contains("timeout_ms"));
}

TEST(AgentWaitToFinishToolTest, WaitToFinish_ImmediateCheck) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    callerAgent->AddChild(child);

    AgentWaitToFinishTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"child1"});
    args["timeout_ms"] = 0;

    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "child1");
    EXPECT_EQ(result[0]["finished"], false);
    EXPECT_EQ(result[0]["killed"], false);
    EXPECT_FALSE(result[0].contains("error"));
    EXPECT_FALSE(result[0].contains("end_time"));
}

TEST(AgentWaitToFinishToolTest, WaitToFinish_WithTimeout_Succeeds) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    child->SetFinished(true);
    callerAgent->AddChild(child);

    AgentWaitToFinishTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"child1"});
    args["timeout_ms"] = 5000;

    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "child1");
    EXPECT_EQ(result[0]["finished"], true);
    EXPECT_EQ(result[0]["killed"], false);
    EXPECT_TRUE(result[0].contains("end_time"));
    EXPECT_FALSE(result[0].contains("error"));
}

TEST(AgentWaitToFinishToolTest, WaitToFinish_ReturnsConsoleAndMessages) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    child->SetFinished(true);
    child->SetConsoleOutput("virtual console content");
    nlohmann::json history = nlohmann::json::array({{{"role", "user"}, {"content", "Hello"}}});
    child->SetHistory(history);
    callerAgent->AddChild(child);

    AgentWaitToFinishTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"child1"});
    args["return_console"] = true;
    args["return_messages"] = true;

    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "child1");
    EXPECT_EQ(result[0]["finished"], true);
    EXPECT_TRUE(result[0].contains("console_result"));
    EXPECT_TRUE(result[0].contains("messages_result"));
    EXPECT_TRUE(result[0]["messages_result"].is_array());
    EXPECT_FALSE(result[0].contains("error"));
}

TEST(AgentWaitToFinishToolTest, WaitToFinish_Forever) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    child->SetFinished(true);
    callerAgent->AddChild(child);

    AgentWaitToFinishTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"child1"});

    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "child1");
    EXPECT_EQ(result[0]["finished"], true);
    EXPECT_EQ(result[0]["killed"], false);
    EXPECT_TRUE(result[0].contains("end_time"));
    EXPECT_FALSE(result[0].contains("error"));
}

TEST(AgentWaitToFinishToolTest, WaitToFinish_AgentNotFound) {
    auto callerAgent = std::make_shared<MockAgent>();
    AgentWaitToFinishTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"nonexistent"});

    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "nonexistent");
    EXPECT_TRUE(result[0].contains("error"));
    EXPECT_EQ(result[0]["error"], "agent nonexistent not found");
}
