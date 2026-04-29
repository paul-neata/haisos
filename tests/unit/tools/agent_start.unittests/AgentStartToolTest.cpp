#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "AgentStartTool.h"
#include "tests/mocks/MockAgent.h"
#include "interfaces/IFactory.h"
#include "interfaces/ILLMCommunicator.h"
#include "interfaces/IToolFactory.h"
#include "interfaces/IConsole.h"
#include "interfaces/IHTTPClient.h"
#include "interfaces/IHaisosEngine.h"

using namespace Haisos;
using namespace Haisos::Tools;
using namespace Haisos::Mocks;

namespace {

class DummyConsole : public IConsole {
public:
    void Write(const std::string&) override {}
    void Write(const IAgent&, const std::string&) override {}
    void Start() override {}
    void Stop() override {}
};

class DummyHTTPClient : public IHTTPClient {
public:
    HTTPResponse Get(const std::string&) override { return HTTPResponse{200, "", ""}; }
    HTTPResponse Post(const std::string&, const std::string&) override { return HTTPResponse{200, "", ""}; }
    HTTPResponse Post(const std::string&, const std::string&, const std::vector<HTTPHeader>&) override { return HTTPResponse{200, "", ""}; }
};

class DummyLLMCommunicator : public ILLMCommunicator {
public:
    LLMResponse Call(const std::vector<LLMMessage>&, const std::vector<std::tuple<std::string, std::string, nlohmann::json>>&, const SystemCallbacks&) override {
        LLMResponse response;
        response.message.role = "assistant";
        response.message.content = "";
        response.done = true;
        return response;
    }
};

class DummyToolFactory : public IToolFactory {
public:
    std::unique_ptr<ITool> CreateTool(const std::string&, std::shared_ptr<IAgent>) override { return nullptr; }
    std::vector<std::string> GetAvailableTools() const override { return {}; }
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override { return {}; }
};

class TestFactory : public IFactory {
public:
    std::unique_ptr<IConsole> CreateConsole(bool) override { return std::make_unique<DummyConsole>(); }
    std::unique_ptr<IHTTPClient> CreateHTTPClient() override { return std::make_unique<DummyHTTPClient>(); }
    std::unique_ptr<ILLMCommunicator> CreateLLMCommunicator(std::unique_ptr<IHTTPClient>, const std::string&, const std::string&, const std::string&) override {
        return std::make_unique<DummyLLMCommunicator>();
    }
    std::unique_ptr<IToolFactory> CreateToolFactory(IFactory&) override { return std::make_unique<DummyToolFactory>(); }
    std::shared_ptr<IAgent> CreateAgent(std::unique_ptr<ILLMCommunicator>, std::unique_ptr<IToolFactory>, std::unique_ptr<IConsole>, const std::vector<std::string>&, const std::string& name, std::shared_ptr<IAgent> parent, const std::string& startTime, bool longRunning) override {
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
    std::unique_ptr<IHaisosEngine> CreateHaisosEngine(IFactory&) override { return nullptr; }

    SystemCallbacks GetSystemCallbacks() const override { return m_callbacks; }
    void SetSystemCallbacks(const SystemCallbacks& callbacks) override { m_callbacks = callbacks; }

    std::shared_ptr<MockAgent> GetLastAgent() const { return m_lastAgent; }
private:
    std::shared_ptr<MockAgent> m_lastAgent;
    SystemCallbacks m_callbacks;
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
    EXPECT_FALSE(schema["properties"].contains("wait_to_finish"));
    EXPECT_FALSE(schema["properties"].contains("wait_to_finish_timeout_ms"));
    EXPECT_FALSE(schema["properties"].contains("return_console"));
    EXPECT_FALSE(schema["properties"].contains("return_messages"));
}

TEST(AgentStartToolTest, StartReturnsName) {
    TestFactory factory;
    auto callerAgent = std::make_shared<MockAgent>();
    AgentStartTool tool(factory);

    nlohmann::json args;
    args["user_prompt"] = "Hello";

    auto result = tool.Call(callerAgent, args);

    EXPECT_FALSE(result.content.empty());
    EXPECT_EQ(result.content, factory.GetLastAgent()->Name());
}

TEST(AgentStartToolTest, StartWithSystemPrompt) {
    TestFactory factory;
    auto callerAgent = std::make_shared<MockAgent>();
    AgentStartTool tool(factory);

    nlohmann::json args;
    args["user_prompt"] = "Hello";
    args["system_prompt"] = "You are a helpful assistant";

    auto result = tool.Call(callerAgent, args);

    EXPECT_FALSE(result.content.empty());
}

TEST(AgentStartToolTest, MissingUserPromptReturnsError) {
    TestFactory factory;
    auto callerAgent = std::make_shared<MockAgent>();
    AgentStartTool tool(factory);

    nlohmann::json args;
    auto result = tool.Call(callerAgent, args);

    EXPECT_TRUE(result.isError);
    EXPECT_TRUE(result.content.find("user_prompt") != std::string::npos);
}

TEST(AgentStartToolTest, RecursionDepthExceededReturnsError) {
    TestFactory factory;
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

    AgentStartTool tool(factory);

    nlohmann::json args;
    args["user_prompt"] = "Hello";

    auto result = tool.Call(p6, args);

    EXPECT_TRUE(result.isError);
    EXPECT_TRUE(result.content.find("depth") != std::string::npos);
}
