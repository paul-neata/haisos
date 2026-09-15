#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "AgentStartTool.h"
#include "tests/mocks/MockAgent.h"
#include "interfaces/ILLMService.h"

using namespace Haisos;
using namespace Haisos::Tools;
using namespace Haisos::Mocks;

namespace {

class DummyAgentConsole : public IAgentConsole {
public:
    void Write(const std::string&) override {}
};

class DummyToolFactory : public IToolFactory {
public:
    std::unique_ptr<ITool> CreateTool(const std::string&, std::shared_ptr<IAgent>) override { return nullptr; }
    bool HasTool(const std::string&) const override { return false; }
    std::vector<std::string> GetAvailableTools() const override { return {}; }
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override { return {}; }
};

class TestLLMService : public ILLMService {
public:
    std::shared_ptr<IAgent> CreateAgent(
        const std::vector<std::string>&,
        const std::string& name,
        std::shared_ptr<IAgent> parent,
        std::unique_ptr<IAgentConsole>,
        const std::string& startTime,
        bool longRunning,
        IToolFactory*) override
    {
        auto agent = std::make_shared<MockAgent>();
        agent->SetName(name);
        agent->SetStartTime(startTime);
        agent->SetLongRunning(longRunning);
        m_lastAgent = agent;
        if (parent) {
            parent->AddChild(agent);
        }
        return agent;
    }

    IToolFactory& GetToolFactory() override { return m_toolFactory; }
    std::unique_ptr<IAgentConsole> CreateAgentConsole() override { return std::make_unique<DummyAgentConsole>(); }

    std::shared_ptr<MockAgent> GetLastAgent() const { return m_lastAgent; }

private:
    DummyToolFactory m_toolFactory;
    std::shared_ptr<MockAgent> m_lastAgent;
};

} // namespace

TEST(AgentStartToolTest, GetParametersSchemaIsValid) {
    auto schema = AgentStartTool::GetDefaultParametersSchema();

    EXPECT_TRUE(schema.is_object());
    EXPECT_EQ(schema.value("type", ""), "object");
    EXPECT_TRUE(schema.contains("properties"));
    EXPECT_TRUE(schema.contains("required"));
    EXPECT_TRUE(schema["properties"].contains("user_prompt"));
    EXPECT_TRUE(schema["properties"].contains("system_prompt"));
    EXPECT_TRUE(schema["properties"].contains("oneShot"));
    EXPECT_FALSE(schema["properties"].contains("wait_to_finish"));
    EXPECT_FALSE(schema["properties"].contains("wait_to_finish_timeout_ms"));
    EXPECT_FALSE(schema["properties"].contains("return_console"));
    EXPECT_FALSE(schema["properties"].contains("return_messages"));
}

TEST(AgentStartToolTest, StartReturnsName) {
    TestLLMService llmService;
    auto callerAgent = std::make_shared<MockAgent>();
    AgentStartTool tool(llmService);

    nlohmann::json args;
    args["user_prompt"] = "Hello";
    args["oneShot"] = true;

    auto result = tool.Call(callerAgent, args);

    EXPECT_FALSE(result.content.empty());
    EXPECT_EQ(result.content, llmService.GetLastAgent()->Name());
}

TEST(AgentStartToolTest, StartWithSystemPrompt) {
    TestLLMService llmService;
    auto callerAgent = std::make_shared<MockAgent>();
    AgentStartTool tool(llmService);

    nlohmann::json args;
    args["user_prompt"] = "Hello";
    args["system_prompt"] = "You are a helpful assistant";
    args["oneShot"] = true;

    auto result = tool.Call(callerAgent, args);

    EXPECT_FALSE(result.content.empty());
}

TEST(AgentStartToolTest, MissingUserPromptReturnsError) {
    TestLLMService llmService;
    auto callerAgent = std::make_shared<MockAgent>();
    AgentStartTool tool(llmService);

    nlohmann::json args;
    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.isError);
    EXPECT_TRUE(result.content.find("user_prompt") != std::string::npos);
}

TEST(AgentStartToolTest, RecursionDepthExceededReturnsError) {
    TestLLMService llmService;
    // Build a chain so the caller depth >= 5
    auto p1 = std::make_shared<MockAgent>();
    auto p2 = std::make_shared<MockAgent>();
    p2->SetParent(p1);
    auto p3 = std::make_shared<MockAgent>();
    p3->SetParent(p2);
    auto p4 = std::make_shared<MockAgent>();
    p4->SetParent(p3);
    auto p5 = std::make_shared<MockAgent>();
    p5->SetParent(p4);
    auto p6 = std::make_shared<MockAgent>();
    p6->SetParent(p5);

    AgentStartTool tool(llmService);

    nlohmann::json args;
    args["user_prompt"] = "Hello";
    args["oneShot"] = true;

    auto result = tool.Call(p6, args);

    EXPECT_TRUE(result.isError);
    EXPECT_TRUE(result.content.find("depth") != std::string::npos);
}
