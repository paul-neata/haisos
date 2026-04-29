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

TEST(AgentWaitToFinishToolTest, WaitToFinish_MissingNames_ReturnsError) {
    auto callerAgent = std::make_shared<MockAgent>();
    AgentWaitToFinishTool tool;

    nlohmann::json args;
    // missing "names"

    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.isError);
    EXPECT_EQ(result.content, "Missing required field: names");
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

    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.content.empty());
    EXPECT_FALSE(result.isError);
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

    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.content.empty());
    EXPECT_FALSE(result.isError);
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

    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.content.empty());
    EXPECT_FALSE(result.isError);
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

    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.content.empty());
    EXPECT_FALSE(result.isError);
}

TEST(AgentWaitToFinishToolTest, WaitToFinish_AgentNotFound) {
    auto callerAgent = std::make_shared<MockAgent>();
    AgentWaitToFinishTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"nonexistent"});

    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.isError);
    EXPECT_EQ(result.content, "nonexistent not found");
}
