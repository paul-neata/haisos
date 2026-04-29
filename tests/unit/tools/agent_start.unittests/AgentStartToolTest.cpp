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
    LLMResponse Call(const std::vector<LLMMessage>&, const std::vector<std::tuple<std::string, std::string, nlohmann::json>>&, const JsonSendReceiveCallbacks&) override {
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
    std::shared_ptr<IAgent> CreateAgent(std::unique_ptr<ILLMCommunicator>, std::unique_ptr<IToolFactory>, std::unique_ptr<IConsole>, const std::vector<std::string>&, const std::string& name, std::shared_ptr<IAgent> parent, const std::string& startTime, const JsonSendReceiveCallbacks&) override {
        auto agent = std::make_shared<MockAgent>();
        agent->SetName(name);
        agent->SetStartTime(startTime);
        m_lastAgent = agent;
        if (parent) {
            parent->AddChild(agent);
        }
        return agent;
    }
    std::unique_ptr<IHaisosEngine> CreateHaisosEngine(IFactory&) override { return nullptr; }

    std::shared_ptr<MockAgent> GetLastAgent() const { return m_lastAgent; }
private:
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
    EXPECT_TRUE(schema["properties"].contains("wait_to_finish"));
    EXPECT_TRUE(schema["properties"].contains("wait_to_finish_timeout_ms"));
}

TEST(AgentStartToolTest, StartReturnsNameAndNotFinished) {
    TestFactory factory;
    auto callerAgent = std::make_shared<MockAgent>();
    AgentStartTool tool(factory);

    nlohmann::json args;
    args["user_prompt"] = "Hello";

    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    EXPECT_TRUE(result.contains("name"));
    EXPECT_FALSE(result["name"].get<std::string>().empty());
    EXPECT_TRUE(result.contains("finished"));
    EXPECT_EQ(result["finished"], false);
    EXPECT_TRUE(result.contains("start_time"));
    EXPECT_TRUE(result.contains("killed"));
    EXPECT_EQ(result["killed"], false);
}

TEST(AgentStartToolTest, WaitToFinishReturnsFinished) {
    TestFactory factory;
    auto callerAgent = std::make_shared<MockAgent>();
    AgentStartTool tool(factory);

    nlohmann::json args;
    args["user_prompt"] = "Hello";
    args["wait_to_finish"] = true;

    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    EXPECT_TRUE(result.contains("finished"));
    EXPECT_EQ(result["finished"], true);
}

TEST(AgentStartToolTest, MissingUserPromptReturnsError) {
    TestFactory factory;
    auto callerAgent = std::make_shared<MockAgent>();
    AgentStartTool tool(factory);

    nlohmann::json args;
    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    EXPECT_TRUE(result.contains("error"));
}

TEST(AgentStartToolTest, StartWithReturnConsoleIncludesConsoleResult) {
    TestFactory factory;
    auto callerAgent = std::make_shared<MockAgent>();
    AgentStartTool tool(factory);

    nlohmann::json args;
    args["user_prompt"] = "Hello";
    args["wait_to_finish"] = true;
    args["return_console"] = true;

    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    EXPECT_TRUE(result.contains("console_result"));
}

TEST(AgentStartToolTest, StartWithReturnMessagesIncludesMessagesResult) {
    TestFactory factory;
    auto callerAgent = std::make_shared<MockAgent>();
    AgentStartTool tool(factory);

    nlohmann::json args;
    args["user_prompt"] = "Hello";
    args["wait_to_finish"] = true;
    args["return_messages"] = true;

    std::string resultStr = tool.Call(callerAgent, args);
    auto result = nlohmann::json::parse(resultStr);

    EXPECT_TRUE(result.contains("messages_result"));
    EXPECT_TRUE(result["messages_result"].is_array());
}
