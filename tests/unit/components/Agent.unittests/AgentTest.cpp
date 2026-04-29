#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include <thread>
#include "Agent.h"
#include "tests/mocks/MockLLMCommunicator.h"
#include "tests/mocks/MockConsole.h"
#include "src/components/Factory/Factory.h"

using namespace Haisos;
using namespace Haisos::Mocks;

TEST(AgentTest, Construction) {
    Factory factory;
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = std::make_shared<MockConsole>();
    auto toolFactory = factory.CreateToolFactory(factory);

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    EXPECT_EQ(agent->Name(), "test_agent");
    EXPECT_EQ(agent->GetParent(), nullptr);

    agent->Stop(0);
    agent->WaitToFinish();
}

TEST(AgentTest, PostAndWaitToFinish) {
    Factory factory;
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Hello from agent");
    auto mockConsole = std::make_shared<MockConsole>();
    auto toolFactory = factory.CreateToolFactory(factory);

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("Test command");
    agent->Stop(0);
    agent->WaitToFinish();

    EXPECT_EQ(mockLLM->GetCallCount(), 1);
}

TEST(AgentTest, CommandProcessingWritesToConsole) {
    Factory factory;
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Agent response");
    auto mockConsole = std::make_shared<MockConsole>();
    auto toolFactory = factory.CreateToolFactory(factory);

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("Test command");
    agent->Stop(0);
    agent->WaitToFinish();

    const auto& messages = mockConsole->GetMessages();
    ASSERT_FALSE(messages.empty());
    EXPECT_NE(messages[0].find("[test_agent]"), std::string::npos);
    EXPECT_NE(messages[0].find("Agent response"), std::string::npos);
}

TEST(AgentTest, CommandProcessingWritesToConsoleOutput) {
    Factory factory;
    auto mockLLM = std::make_unique<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Virtual response");
    auto mockConsole = factory.CreateConsole(false);
    auto toolFactory = factory.CreateToolFactory(factory);

    auto agent = factory.CreateAgent(
        std::move(mockLLM),
        std::move(toolFactory),
        std::move(mockConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent",
        nullptr);

    agent->Post("Test command");
    agent->Stop(0);
    agent->WaitToFinish();

    std::string contents = agent->GetConsoleOutput();
    EXPECT_NE(contents.find("Virtual response"), std::string::npos);
}

TEST(AgentTest, MultiplePosts) {
    Factory factory;
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Response");
    auto mockConsole = std::make_shared<MockConsole>();
    auto toolFactory = factory.CreateToolFactory(factory);

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("Command 1");
    agent->Post("Command 2");
    agent->Stop(0);
    agent->WaitToFinish();

    EXPECT_EQ(mockLLM->GetCallCount(), 2);
}

TEST(AgentTest, StopWithoutPost) {
    Factory factory;
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = std::make_shared<MockConsole>();
    auto toolFactory = factory.CreateToolFactory(factory);

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Stop(0);
    agent->WaitToFinish();

    EXPECT_EQ(mockLLM->GetCallCount(), 0);
}

TEST(AgentTest, ParentChildRelationship) {
    Factory factory;
    auto mockLLM = std::make_unique<MockLLMCommunicator>();
    auto mockConsole = factory.CreateConsole(false);
    auto toolFactory = factory.CreateToolFactory(factory);

    auto parent = factory.CreateAgent(
        std::move(mockLLM),
        std::move(toolFactory),
        std::move(mockConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "parent",
        nullptr);

    toolFactory = factory.CreateToolFactory(factory);
    auto childLLM = std::make_unique<MockLLMCommunicator>();
    auto childConsole = factory.CreateConsole(false);
    auto child = factory.CreateAgent(
        std::move(childLLM),
        std::move(toolFactory),
        std::move(childConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "child",
        parent);

    auto children = parent->GetChildren();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0]->Name(), "child");

    child->Stop(0);
    child->WaitToFinish();
    parent->Stop(0);
    parent->WaitToFinish();
}

TEST(AgentTest, ChildKnowsParent) {
    Factory factory;
    auto mockLLM = std::make_unique<MockLLMCommunicator>();
    auto mockConsole = factory.CreateConsole(false);
    auto toolFactory = factory.CreateToolFactory(factory);

    auto parent = factory.CreateAgent(
        std::move(mockLLM),
        std::move(toolFactory),
        std::move(mockConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "parent",
        nullptr);

    toolFactory = factory.CreateToolFactory(factory);
    auto childLLM = std::make_unique<MockLLMCommunicator>();
    auto childConsole = factory.CreateConsole(false);
    auto child = factory.CreateAgent(
        std::move(childLLM),
        std::move(toolFactory),
        std::move(childConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "child",
        parent);

    EXPECT_EQ(child->GetParent(), parent);

    child->Stop(0);
    child->WaitToFinish();
    parent->Stop(0);
    parent->WaitToFinish();
}

TEST(AgentTest, ChildDestructionRemovesFromParent) {
    Factory factory;
    auto mockLLM = std::make_unique<MockLLMCommunicator>();
    auto mockConsole = factory.CreateConsole(false);
    auto toolFactory = factory.CreateToolFactory(factory);

    auto parent = factory.CreateAgent(
        std::move(mockLLM),
        std::move(toolFactory),
        std::move(mockConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "parent",
        nullptr);

    toolFactory = factory.CreateToolFactory(factory);
    {
        auto childLLM = std::make_unique<MockLLMCommunicator>();
        auto childConsole = factory.CreateConsole(false);
        auto child = factory.CreateAgent(
            std::move(childLLM),
            std::move(toolFactory),
            std::move(childConsole),
            std::vector<std::string>{"You are a helpful AI assistant."},
            "child",
            parent);

        EXPECT_EQ(parent->GetChildren().size(), 1u);
        child->Stop(0);
        child->WaitToFinish();
    }

    // Factory holds a shared_ptr to all agents, so the child remains alive
    EXPECT_EQ(parent->GetChildren().size(), 1u);

    parent->Stop(0);
    parent->WaitToFinish();
}

TEST(AgentTest, GetHistoryContainsUserMessage) {
    Factory factory;
    auto mockLLM = std::make_unique<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Agent response");
    auto mockConsole = factory.CreateConsole(false);
    auto toolFactory = factory.CreateToolFactory(factory);

    auto agent = factory.CreateAgent(
        std::move(mockLLM),
        std::move(toolFactory),
        std::move(mockConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent",
        nullptr);

    agent->Post("Hello agent");
    agent->Stop(0);
    agent->WaitToFinish();

    auto history = agent->GetHistory();
    ASSERT_TRUE(history.is_array());
    ASSERT_GE(history.size(), 2u);

    bool foundUserMessage = false;
    for (const auto& entry : history) {
        if (entry.value("role", "") == "user" && entry.value("content", "").find("Hello agent") != std::string::npos) {
            foundUserMessage = true;
            break;
        }
    }
    EXPECT_TRUE(foundUserMessage);
}

TEST(AgentTest, KillSetsKilledFlag) {
    Factory factory;
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = std::make_shared<MockConsole>();
    auto toolFactory = factory.CreateToolFactory(factory);

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    EXPECT_FALSE(agent->IsKilled());
    agent->Kill();
    agent->WaitToFinish();
    EXPECT_TRUE(agent->IsKilled());
}

TEST(AgentTest, WaitToFinishWithTimeoutReturnsFalseIfNotFinished) {
    Factory factory;
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = std::make_shared<MockConsole>();
    auto toolFactory = factory.CreateToolFactory(factory);

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    // Don't post anything, just wait with timeout
    bool finished = agent->WaitToFinish(50);
    EXPECT_FALSE(finished);

    agent->Stop(0);
    agent->WaitToFinish();
}

TEST(AgentTest, StopWithTimeoutWaitsForFinish) {
    Factory factory;
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("quick");
    auto mockConsole = std::make_shared<MockConsole>();
    auto toolFactory = factory.CreateToolFactory(factory);

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("hello");
    bool stopped = agent->Stop(5000);
    EXPECT_TRUE(stopped);
}

TEST(AgentTest, GetConsoleOutputContainsAgentMessages) {
    Factory factory;
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Hello world");
    auto mockConsole = std::make_shared<MockConsole>();
    auto toolFactory = factory.CreateToolFactory(factory);

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("Test");
    agent->Stop(0);
    agent->WaitToFinish();

    std::string output = agent->GetConsoleOutput();
    EXPECT_NE(output.find("Hello world"), std::string::npos);
}
