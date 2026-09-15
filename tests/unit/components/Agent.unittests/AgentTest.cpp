#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include <thread>
#include "Agent.h"
#include "tests/mocks/MockLLMCommunicator.h"
#include "tests/mocks/MockAgentConsole.h"
#include "src/components/ToolFactory/ToolFactory.h"
#include "src/components/Console/InMemoryAgentConsole.h"

using namespace Haisos;
using namespace Haisos::Mocks;

TEST(AgentTest, Construction) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    EXPECT_EQ(agent->Name(), "test_agent");
    EXPECT_EQ(agent->GetParent(), nullptr);

    agent->Stop(0);
    agent->WaitToFinish();
}

TEST(AgentTest, PostAndWaitToFinish) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Hello from agent");
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("Test command");
    agent->Stop(0);
    agent->WaitToFinish();

    EXPECT_EQ(mockLLM->GetCallCount(), 1);
}

TEST(AgentTest, CommandProcessingWritesToConsole) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Agent response");
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("Test command");
    agent->Stop(0);
    agent->WaitToFinish();

    // IAgentConsole receives the raw message; per-source tagging (e.g. "[name]")
    // is the physical console's job (see AgentConsoleAdapter/Console), not the
    // agent's -- otherwise a physical-console-backed agent would get tagged twice.
    const auto& messages = mockConsole->GetMessages();
    ASSERT_FALSE(messages.empty());
    EXPECT_EQ(messages[0].find("[test_agent]"), std::string::npos);
    EXPECT_NE(messages[0].find("Agent response"), std::string::npos);
}

TEST(AgentTest, CommandProcessingWritesToConsoleOutput) {
    auto mockLLM = std::make_unique<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Virtual response");
    auto mockConsole = std::make_unique<InMemoryAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

    auto agent = std::make_shared<Agent>(
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
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Response");
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

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
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Stop(0);
    agent->WaitToFinish();

    EXPECT_EQ(mockLLM->GetCallCount(), 0);
}

TEST(AgentTest, ParentChildRelationship) {
    auto mockLLM = std::make_unique<MockLLMCommunicator>();
    auto mockConsole = std::make_unique<InMemoryAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

    auto parent = std::make_shared<Agent>(
        std::move(mockLLM),
        std::move(toolFactory),
        std::move(mockConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "parent",
        nullptr);

    auto childLLM = std::make_unique<MockLLMCommunicator>();
    auto childConsole = std::make_unique<InMemoryAgentConsole>();
    auto childToolFactory = std::make_unique<ToolFactory>();
    auto child = std::make_shared<Agent>(
        std::move(childLLM),
        std::move(childToolFactory),
        std::move(childConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "child",
        parent);
    parent->AddChild(child);

    auto children = parent->GetChildren();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0]->Name(), "child");

    child->Stop(0);
    child->WaitToFinish();
    parent->Stop(0);
    parent->WaitToFinish();
}

TEST(AgentTest, ChildKnowsParent) {
    auto mockLLM = std::make_unique<MockLLMCommunicator>();
    auto mockConsole = std::make_unique<InMemoryAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

    auto parent = std::make_shared<Agent>(
        std::move(mockLLM),
        std::move(toolFactory),
        std::move(mockConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "parent",
        nullptr);

    auto childLLM = std::make_unique<MockLLMCommunicator>();
    auto childConsole = std::make_unique<InMemoryAgentConsole>();
    auto childToolFactory = std::make_unique<ToolFactory>();
    auto child = std::make_shared<Agent>(
        std::move(childLLM),
        std::move(childToolFactory),
        std::move(childConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "child",
        parent);
    parent->AddChild(child);

    EXPECT_EQ(child->GetParent(), parent);

    child->Stop(0);
    child->WaitToFinish();
    parent->Stop(0);
    parent->WaitToFinish();
}

TEST(AgentTest, ChildDestructionRemovesFromParent) {
    auto mockLLM = std::make_unique<MockLLMCommunicator>();
    auto mockConsole = std::make_unique<InMemoryAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

    auto parent = std::make_shared<Agent>(
        std::move(mockLLM),
        std::move(toolFactory),
        std::move(mockConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "parent",
        nullptr);

    // Unlike the old Factory-backed flow (which kept every created agent
    // alive internally), nothing here holds the child alive once it goes out
    // of scope except parent's own weak_ptr -- so this test now specifically
    // verifies that GetChildren() prunes a child that's actually gone.
    {
        auto childLLM = std::make_unique<MockLLMCommunicator>();
        auto childConsole = std::make_unique<InMemoryAgentConsole>();
        auto childToolFactory = std::make_unique<ToolFactory>();
        auto child = std::make_shared<Agent>(
            std::move(childLLM),
            std::move(childToolFactory),
            std::move(childConsole),
            std::vector<std::string>{"You are a helpful AI assistant."},
            "child",
            parent);
        parent->AddChild(child);

        EXPECT_EQ(parent->GetChildren().size(), 1u);
        child->Stop(0);
        child->WaitToFinish();
    }

    EXPECT_EQ(parent->GetChildren().size(), 0u);

    parent->Stop(0);
    parent->WaitToFinish();
}

TEST(AgentTest, GetHistoryContainsUserMessage) {
    auto mockLLM = std::make_unique<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Agent response");
    auto mockConsole = std::make_unique<InMemoryAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

    auto agent = std::make_shared<Agent>(
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
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    EXPECT_FALSE(agent->IsKilled());
    agent->Kill();
    agent->WaitToFinish();
    EXPECT_TRUE(agent->IsKilled());
}

TEST(AgentTest, WaitToFinishWithTimeoutReturnsFalseIfNotFinished) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

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
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("quick");
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("hello");
    bool stopped = agent->Stop(5000);
    EXPECT_TRUE(stopped);
}

TEST(AgentTest, GetConsoleOutputContainsAgentMessages) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Hello world");
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = std::make_unique<ToolFactory>();

    auto agent = std::make_shared<Agent>(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("Test");
    agent->Stop(0);
    agent->WaitToFinish();

    std::string output = agent->GetConsoleOutput();
    EXPECT_NE(output.find("Hello world"), std::string::npos);
}
