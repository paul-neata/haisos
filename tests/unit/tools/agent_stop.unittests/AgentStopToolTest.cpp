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

    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "child1");
    EXPECT_EQ(result[0]["killed"], false);
    EXPECT_FALSE(result[0].contains("error"));
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

    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "child1");
    EXPECT_FALSE(result[0].contains("error"));
}
