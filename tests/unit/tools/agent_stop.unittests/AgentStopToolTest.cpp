#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "AgentStopTool.h"
#include "tests/mocks/MockAgent.h"

using namespace Haisos;
using namespace Haisos::Tools;
using namespace Haisos::Mocks;

TEST(AgentStopToolTest, GetParametersSchemaIsValid) {
    auto schema = AgentStopTool::GetDefaultParametersSchema();

    EXPECT_TRUE(schema.is_object());
    EXPECT_EQ(schema.value("type", ""), "object");
    EXPECT_TRUE(schema.contains("properties"));
    EXPECT_TRUE(schema.contains("required"));
    EXPECT_TRUE(schema["properties"].contains("names"));
    EXPECT_TRUE(schema["properties"].contains("kill"));
}

TEST(AgentStopToolTest, StopOnExistingAgent) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    callerAgent->AddChild(child);

    AgentStopTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"child1"});

    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.content.empty());
    EXPECT_FALSE(result.isError);
    EXPECT_TRUE(child->WasStopped());
}

TEST(AgentStopToolTest, KillOnExistingAgent) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    callerAgent->AddChild(child);

    AgentStopTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"child1"});
    args["kill"] = true;

    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.content.empty());
    EXPECT_FALSE(result.isError);
    EXPECT_TRUE(child->IsKilled());
}

TEST(AgentStopToolTest, StopOnNotFoundAgent) {
    auto callerAgent = std::make_shared<MockAgent>();

    AgentStopTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"missing"});

    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.content.empty());
    EXPECT_FALSE(result.isError);
}

TEST(AgentStopToolTest, StopOnAlreadyStoppedAgentIsNotError) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    child->SetFinished(true);
    callerAgent->AddChild(child);

    AgentStopTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"child1"});

    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.content.empty());
    EXPECT_FALSE(result.isError);
}

TEST(AgentStopToolTest, StopMultipleAgentsMixedFoundAndNotFound) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child1 = std::make_shared<MockAgent>();
    child1->SetName("agent1");
    callerAgent->AddChild(child1);
    auto child2 = std::make_shared<MockAgent>();
    child2->SetName("agent2");
    child2->SetKilled(true);
    callerAgent->AddChild(child2);

    AgentStopTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"agent1", "missing", "agent2"});

    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.content.empty());
    EXPECT_FALSE(result.isError);
}

TEST(AgentStopToolTest, MissingNamesReturnsEmptyString) {
    auto callerAgent = std::make_shared<MockAgent>();

    AgentStopTool tool;

    nlohmann::json args;
    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.content.empty());
    EXPECT_FALSE(result.isError);
}
