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
    callerAgent->AddChild(child);

    AgentListRunningTool tool;

    nlohmann::json args;
    auto result = tool.Call(callerAgent, args);
    std::string resultStr = result.content;

    EXPECT_EQ(resultStr, "child1");
}

TEST(AgentListRunningToolTest, ListRunningEmptyAfterStop) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child = std::make_shared<MockAgent>();
    child->SetName("child1");
    child->SetFinished(true);
    callerAgent->AddChild(child);

    AgentListRunningTool tool;

    nlohmann::json args;
    auto result = tool.Call(callerAgent, args);
    std::string resultStr = result.content;

    EXPECT_EQ(resultStr, "");
}

TEST(AgentListRunningToolTest, ListRunningWithNameFilter) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child1 = std::make_shared<MockAgent>();
    child1->SetName("alpha");
    callerAgent->AddChild(child1);

    auto child2 = std::make_shared<MockAgent>();
    child2->SetName("beta");
    callerAgent->AddChild(child2);

    AgentListRunningTool tool;

    nlohmann::json args;
    args["names"] = nlohmann::json::array({"alpha"});

    auto result = tool.Call(callerAgent, args);
    std::string resultStr = result.content;

    EXPECT_EQ(resultStr, "alpha");
}

TEST(AgentListRunningToolTest, ListRunningMultipleAgents) {
    auto callerAgent = std::make_shared<MockAgent>();
    auto child1 = std::make_shared<MockAgent>();
    child1->SetName("alpha");
    callerAgent->AddChild(child1);

    auto child2 = std::make_shared<MockAgent>();
    child2->SetName("beta");
    callerAgent->AddChild(child2);

    auto child3 = std::make_shared<MockAgent>();
    child3->SetName("gamma");
    child3->SetFinished(true);
    callerAgent->AddChild(child3);

    AgentListRunningTool tool;

    nlohmann::json args;
    auto result = tool.Call(callerAgent, args);
    std::string resultStr = result.content;

    EXPECT_EQ(resultStr, "alpha,beta");
}

TEST(AgentListRunningToolTest, ListRunningNoCallerAgent) {
    AgentListRunningTool tool;

    nlohmann::json args;
    auto result = tool.Call(nullptr, args);

    EXPECT_TRUE(result.isError);
    EXPECT_EQ(result.content, "no caller agent");
}
