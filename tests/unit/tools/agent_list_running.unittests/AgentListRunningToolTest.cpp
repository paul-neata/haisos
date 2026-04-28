#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "AgentListRunningTool.h"
#include "tests/mocks/MockAgent.h"

using namespace Haisos;
using namespace Haisos::Tools;
using namespace Haisos::Mocks;

TEST(AgentListRunningToolTest, GetParametersSchemaIsValid) {
    auto schema = AgentListRunningTool::GetDefaultParametersSchema();

    EXPECT_TRUE(schema.is_object());
    EXPECT_EQ(schema.value("type", ""), "object");
    EXPECT_TRUE(schema.contains("properties"));
    EXPECT_TRUE(schema["properties"].contains("names"));
}

TEST(AgentListRunningToolTest, ListRunningReturnsAgent) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    child->SetStartTime("2026-04-28 12:00:00");
    callerAgent->RegisterChild(child);

    AgentListRunningTool tool;

    nlohmann::json args;
    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "child1");
    EXPECT_TRUE(result[0].contains("start_time"));
}

TEST(AgentListRunningToolTest, ListRunningEmptyAfterStop) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    child->SetFinished(true);
    callerAgent->RegisterChild(child);

    AgentListRunningTool tool;

    nlohmann::json args;
    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    ASSERT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 0u);
}

TEST(AgentListRunningToolTest, ListRunningWithNameFilter) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child1 = std::make_shared<MockAgent>();
    child1->SetName("alpha");
    child1->SetStartTime("2026-04-28 12:00:00");
    callerAgent->RegisterChild(child1);

    auto child2 = std::make_shared<MockAgent>();
    child2->SetName("beta");
    child2->SetStartTime("2026-04-28 12:01:00");
    callerAgent->RegisterChild(child2);

    AgentListRunningTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"alpha"});

    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "alpha");
}
