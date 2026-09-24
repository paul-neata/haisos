#include <gtest/gtest.h>
#include "SelfCloseTool.h"
#include "tests/mocks/MockAgent.h"

using namespace Haisos;
using namespace Haisos::Tools;
using namespace Haisos::Mocks;

TEST(SelfCloseToolTest, ToolNameIsCorrect) {
    EXPECT_EQ(SelfCloseTool::ToolName, "self_close");
}

TEST(SelfCloseToolTest, ToolDefaultDescriptionIsSet) {
    EXPECT_FALSE(SelfCloseTool::ToolDefaultDescription.empty());
}

TEST(SelfCloseToolTest, ParametersSchemaTakesNoArguments) {
    auto schema = SelfCloseTool::Create()->GetParametersSchema();
    EXPECT_EQ(schema.value("type", ""), "object");
    ASSERT_TRUE(schema.contains("properties"));
    EXPECT_TRUE(schema["properties"].empty());
}

TEST(SelfCloseToolTest, StopsTheCallingAgent) {
    auto agent = std::make_shared<MockAgent>();
    auto result = SelfCloseTool::Create()->Call(agent, nlohmann::json::object());

    EXPECT_FALSE(result.isError);
    EXPECT_TRUE(agent->WasStopTriggered());
    // It asks and returns: the agent is the one running the tool, so waiting
    // for it here could never succeed.
    EXPECT_FALSE(agent->WasWaitedWithTimeout());
}

TEST(SelfCloseToolTest, WithoutACallingAgentIsAnError) {
    auto result = SelfCloseTool::Create()->Call(nullptr, nlohmann::json::object());
    EXPECT_TRUE(result.isError);
}
