#include <gtest/gtest.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <chrono>
#include <thread>
#include "Agent.h"
#include "tests/mocks/MockLLMCommunicator.h"
#include "tests/mocks/MockAgentConsole.h"
#include "src/components/ToolFactory/ToolFactory.h"
#include "src/components/Console/InMemoryAgentConsole.h"

using namespace Haisos;
using namespace Haisos::Mocks;

namespace {

// The untimed WaitToFinish() is gone, so a test that means "wait until it is
// done" waits with a timeout generous enough that only a real hang trips it.
constexpr uint64_t kWaitTimeoutMs = 5000;

// A tool factory with one tool that records whether it was ever called.
class RecordingToolFactory : public IToolFactory {
public:
    static constexpr const char* kToolName = "recorded_tool";

    bool WasToolCalled() const { return m_called->load(); }

    std::shared_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent>) override {
        if (name != kToolName) {
            return nullptr;
        }
        return std::make_shared<RecordingTool>(m_called);
    }
    bool HasTool(const std::string& name) const override { return name == kToolName; }
    std::vector<std::string> GetAvailableTools() const override { return {kToolName}; }
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override {
        return {{kToolName, "records that it ran", nlohmann::json::object()}};
    }

private:
    class RecordingTool : public ITool {
    public:
        explicit RecordingTool(std::shared_ptr<std::atomic<bool>> called) : m_called(std::move(called)) {}
        ToolResult Call(std::shared_ptr<IAgent>, const nlohmann::json&) override {
            *m_called = true;
            return ToolResult{"ran", false};
        }
        nlohmann::json GetParametersSchema() const override { return nlohmann::json::object(); }

    private:
        std::shared_ptr<std::atomic<bool>> m_called;
    };

    // Shared so a tool outlives the factory call that made it.
    std::shared_ptr<std::atomic<bool>> m_called = std::make_shared<std::atomic<bool>>(false);
};

// What a SelfReleasingToolFactory, its tool and a test share.
struct SelfReleaseState {
    std::mutex mutex;
    std::condition_variable cv;
    // The test's one reference to the agent, until the tool lets go of it.
    std::shared_ptr<IAgent> agent;
    std::thread::id agentThread;
    std::thread::id factoryDestroyedOn;
    bool factoryDestroyed = false;
};

// A tool factory whose one tool lets go of SelfReleaseState::agent, so that the
// references the agent's own thread holds for the call are the last ones. The
// agent owns the factory, so the factory's destructor runs wherever the agent is
// destroyed, and records where that was.
class SelfReleasingToolFactory : public IToolFactory {
public:
    static constexpr const char* kToolName = "let_go_of_the_agent";

    explicit SelfReleasingToolFactory(std::shared_ptr<SelfReleaseState> state) : m_state(std::move(state)) {}
    ~SelfReleasingToolFactory() override {
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->factoryDestroyedOn = std::this_thread::get_id();
            m_state->factoryDestroyed = true;
        }
        m_state->cv.notify_all();
    }

    std::shared_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent>) override {
        return name == kToolName ? std::make_shared<ReleasingTool>(m_state) : nullptr;
    }
    bool HasTool(const std::string& name) const override { return name == kToolName; }
    std::vector<std::string> GetAvailableTools() const override { return {kToolName}; }
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override {
        return {{kToolName, "lets go of the test's reference to the agent", nlohmann::json::object()}};
    }

private:
    class ReleasingTool : public ITool {
    public:
        explicit ReleasingTool(std::shared_ptr<SelfReleaseState> state) : m_state(std::move(state)) {}
        ToolResult Call(std::shared_ptr<IAgent>, const nlohmann::json&) override {
            std::shared_ptr<IAgent> released;
            {
                std::unique_lock<std::mutex> lock(m_state->mutex);
                // The test hands its reference over once it is done with the agent.
                m_state->cv.wait_for(lock, std::chrono::milliseconds(kWaitTimeoutMs), [this] { return m_state->agent != nullptr; });
                m_state->agentThread = std::this_thread::get_id();
                released = std::move(m_state->agent);
            }
            return ToolResult{"let go", false};
        }
        nlohmann::json GetParametersSchema() const override { return nlohmann::json::object(); }

    private:
        std::shared_ptr<SelfReleaseState> m_state;
    };

    std::shared_ptr<SelfReleaseState> m_state;
};

} // namespace

TEST(AgentTest, Construction) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = ToolFactory::Create();

    auto agent = Agent::Create(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    EXPECT_EQ(agent->Name(), "test_agent");
    EXPECT_EQ(agent->GetParent(), nullptr);

    agent->TriggerStop();
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));
}

TEST(AgentTest, PostAndWaitToFinish) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Hello from agent");
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = ToolFactory::Create();

    auto agent = Agent::Create(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("Test command");
    agent->TriggerStop();
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    EXPECT_EQ(mockLLM->GetCallCount(), 1);
}

TEST(AgentTest, CommandProcessingWritesToConsole) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Agent response");
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = ToolFactory::Create();

    auto agent = Agent::Create(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("Test command");
    agent->TriggerStop();
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    // IAgentConsole receives the raw message; per-source tagging (e.g. "[name]")
    // is the physical console's job (see AgentConsoleAdapter/Console), not the
    // agent's -- otherwise a physical-console-backed agent would get tagged twice.
    const auto& messages = mockConsole->GetMessages();
    ASSERT_FALSE(messages.empty());
    EXPECT_EQ(messages[0].find("[test_agent]"), std::string::npos);
    EXPECT_NE(messages[0].find("Agent response"), std::string::npos);
}

TEST(AgentTest, CommandProcessingWritesToConsoleOutput) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Virtual response");
    auto mockConsole = InMemoryAgentConsole::Create();
    auto toolFactory = ToolFactory::Create();

    auto agent = Agent::Create(
        std::move(mockLLM),
        std::move(toolFactory),
        std::move(mockConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent",
        nullptr);

    agent->Post("Test command");
    agent->TriggerStop();
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    std::string contents = agent->GetConsoleOutput();
    EXPECT_NE(contents.find("Virtual response"), std::string::npos);
}

TEST(AgentTest, MultiplePosts) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Response");
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = ToolFactory::Create();

    auto agent = Agent::Create(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("Command 1");
    agent->Post("Command 2");
    agent->TriggerStop();
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    EXPECT_EQ(mockLLM->GetCallCount(), 2);
}

TEST(AgentTest, StopWithoutPost) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = ToolFactory::Create();

    auto agent = Agent::Create(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->TriggerStop();
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    EXPECT_EQ(mockLLM->GetCallCount(), 0);
}

TEST(AgentTest, ParentChildRelationship) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = InMemoryAgentConsole::Create();
    auto toolFactory = ToolFactory::Create();

    auto parent = Agent::Create(
        std::move(mockLLM),
        std::move(toolFactory),
        std::move(mockConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "parent",
        nullptr);

    auto childLLM = std::make_shared<MockLLMCommunicator>();
    auto childConsole = InMemoryAgentConsole::Create();
    auto childToolFactory = ToolFactory::Create();
    auto child = Agent::Create(
        std::move(childLLM),
        std::move(childToolFactory),
        std::move(childConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "child",
        parent);
    parent->AddChild(child);

    auto children = parent->GetChildren(/*onlyDirectChildren=*/true);
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0]->Name(), "child");

    child->TriggerStop();
    ASSERT_TRUE(child->WaitToFinish(kWaitTimeoutMs));
    parent->TriggerStop();
    ASSERT_TRUE(parent->WaitToFinish(kWaitTimeoutMs));
}

TEST(AgentTest, ChildKnowsParent) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = InMemoryAgentConsole::Create();
    auto toolFactory = ToolFactory::Create();

    auto parent = Agent::Create(
        std::move(mockLLM),
        std::move(toolFactory),
        std::move(mockConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "parent",
        nullptr);

    auto childLLM = std::make_shared<MockLLMCommunicator>();
    auto childConsole = InMemoryAgentConsole::Create();
    auto childToolFactory = ToolFactory::Create();
    auto child = Agent::Create(
        std::move(childLLM),
        std::move(childToolFactory),
        std::move(childConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "child",
        parent);
    parent->AddChild(child);

    EXPECT_EQ(child->GetParent(), parent);

    child->TriggerStop();
    ASSERT_TRUE(child->WaitToFinish(kWaitTimeoutMs));
    parent->TriggerStop();
    ASSERT_TRUE(parent->WaitToFinish(kWaitTimeoutMs));
}

TEST(AgentTest, ChildDestructionRemovesFromParent) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = InMemoryAgentConsole::Create();
    auto toolFactory = ToolFactory::Create();

    auto parent = Agent::Create(
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
        auto childLLM = std::make_shared<MockLLMCommunicator>();
        auto childConsole = InMemoryAgentConsole::Create();
        auto childToolFactory = ToolFactory::Create();
        auto child = Agent::Create(
            std::move(childLLM),
            std::move(childToolFactory),
            std::move(childConsole),
            std::vector<std::string>{"You are a helpful AI assistant."},
            "child",
            parent);
        parent->AddChild(child);

        EXPECT_EQ(parent->GetChildren(/*onlyDirectChildren=*/true).size(), 1u);
        child->TriggerStop();
        ASSERT_TRUE(child->WaitToFinish(kWaitTimeoutMs));
    }

    EXPECT_EQ(parent->GetChildren(/*onlyDirectChildren=*/true).size(), 0u);

    parent->TriggerStop();
    ASSERT_TRUE(parent->WaitToFinish(kWaitTimeoutMs));
}

TEST(AgentTest, GetHistoryContainsUserMessage) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Agent response");
    auto mockConsole = InMemoryAgentConsole::Create();
    auto toolFactory = ToolFactory::Create();

    auto agent = Agent::Create(
        std::move(mockLLM),
        std::move(toolFactory),
        std::move(mockConsole),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent",
        nullptr);

    agent->Post("Hello agent");
    agent->TriggerStop();
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

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

TEST(AgentTest, TriggerStopFinishesAnIdleAgent) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = ToolFactory::Create();

    auto agent = Agent::Create(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    EXPECT_FALSE(agent->IsFinished());
    agent->TriggerStop();
    EXPECT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));
    EXPECT_TRUE(agent->IsFinished());
}

TEST(AgentTest, WaitToFinishWithTimeoutReturnsFalseIfNotFinished) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = ToolFactory::Create();

    auto agent = Agent::Create(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    // Don't post anything, just wait with timeout
    bool finished = agent->WaitToFinish(50);
    EXPECT_FALSE(finished);

    agent->TriggerStop();
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));
}

TEST(AgentTest, TriggerStopThenWaitToFinishReturnsTrue) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("quick");
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = ToolFactory::Create();

    auto agent = Agent::Create(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("hello");
    agent->TriggerStop();
    bool stopped = agent->WaitToFinish(5000);
    EXPECT_TRUE(stopped);
}

// A stop asked for while a command is in flight is noticed before the next tool
// runs, not only at the next command -- which for a command that keeps calling
// tools would be a long way off. Every call still gets an answer, so the history
// keeps one result per tool call.
TEST(AgentTest, StopRequestedMidRoundStopsToolsFromRunning) {
    auto toolFactory = std::make_shared<RecordingToolFactory>();
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetToolCallResponse(RecordingToolFactory::kToolName);

    auto agent = Agent::Create(
        mockLLM,
        toolFactory,
        InMemoryAgentConsole::Create(),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent",
        nullptr);

    // Fires on the agent's own thread, inside the LLM call of the round whose
    // tool calls are about to be executed.
    std::weak_ptr<Agent> weakAgent = agent;
    mockLLM->SetOnCall([weakAgent] {
        if (auto live = weakAgent.lock()) {
            live->TriggerStop();
        }
    });

    agent->Post("do some work");
    EXPECT_TRUE(agent->WaitToFinish(5000));
    EXPECT_FALSE(toolFactory->WasToolCalled());
}

// Without the stop, the same arrangement does run the tool -- so the test above
// is pinning the stop, not some other reason the tool never ran.
TEST(AgentTest, ToolsRunNormallyWhenNoStopIsRequested) {
    auto toolFactory = std::make_shared<RecordingToolFactory>();
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetToolCallResponse(RecordingToolFactory::kToolName);

    // Not interactive, so it finishes on its own once the command is answered --
    // no stop is needed, and none can race ahead of the round.
    auto agent = Agent::Create(
        mockLLM,
        toolFactory,
        InMemoryAgentConsole::Create(),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent",
        nullptr,
        /*startTime=*/"",
        /*interactive=*/false);

    agent->Post("do some work");
    EXPECT_TRUE(agent->WaitToFinish(5000));
    EXPECT_TRUE(toolFactory->WasToolCalled());
}

TEST(AgentTest, GetConsoleOutputContainsAgentMessages) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("Hello world");
    auto mockConsole = std::make_shared<MockAgentConsole>();
    auto toolFactory = ToolFactory::Create();

    auto agent = Agent::Create(mockLLM, std::move(toolFactory), mockConsole,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr);

    agent->Post("Test");
    agent->TriggerStop();
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    std::string output = agent->GetConsoleOutput();
    EXPECT_NE(output.find("Hello world"), std::string::npos);
}

// An interactive agent never finishes by answering; self_close is how it ends
// itself, through the real tool factory's tool.
TEST(AgentTest, AnInteractiveAgentFinishesWhenItCallsSelfClose) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetToolCallResponse("self_close");

    auto agent = Agent::Create(
        mockLLM,
        ToolFactory::Create(),
        InMemoryAgentConsole::Create(),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent",
        nullptr,
        /*startTime=*/"",
        /*interactive=*/true);

    agent->Post("we are done, close yourself");
    EXPECT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));
    // The round ends with the tool call: no further LLM call is made once the
    // agent has asked to stop.
    EXPECT_EQ(mockLLM->GetCallCount(), 1);
}

// The agent side of the problem written up in HaisosOSTest.cpp, under "Objects
// released last on their own threads".
//
// An agent hands shared_from_this() to every tool it calls, so for the length
// of a call its own thread holds a reference to it. If everyone else lets go
// meanwhile -- here the tool itself lets go of the test's reference -- that
// reference is the last, and it goes when the call returns, on the agent's own
// thread. Before the fix, ~Agent then ran right there: it stops the agent and
// waits for its thread in a loop that never gives up, and since it was running
// on that very thread, it hung forever, logging "Agent 'self_releasing' has not
// stopped after waiting 5000ms in its destructor, still waiting" every 5 s.
// This test failed after 10 s, the agent never destroyed.
//
// With the fix (DestroyOffRuntimeThreads, in Agent::Create), the release hands
// the agent to the destruction thread, and the agent's thread carries on: it
// sees the stop, ends its round and finishes, and only then does ~Agent, on the
// destruction thread, get past its wait. The factory, which the agent owns,
// records which thread that was.
TEST(AgentTest, AnAgentReleasedLastByItsOwnToolCallIsDestroyedOffItsThread) {
    auto state = std::make_shared<SelfReleaseState>();
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetToolCallResponse(SelfReleasingToolFactory::kToolName);

    auto agent = Agent::Create(
        mockLLM,
        std::make_shared<SelfReleasingToolFactory>(state),
        std::make_shared<MockAgentConsole>(),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "self_releasing",
        nullptr,
        /*startTime=*/"",
        /*interactive=*/false);
    agent->Post("let go of yourself");
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->agent = std::move(agent);
    }
    state->cv.notify_all();

    std::unique_lock<std::mutex> lock(state->mutex);
    ASSERT_TRUE(state->cv.wait_for(lock, std::chrono::milliseconds(2 * kWaitTimeoutMs), [&] { return state->factoryDestroyed; }))
        << "the agent was never destroyed: its destructor is waiting for its own thread";
    EXPECT_NE(state->factoryDestroyedOn, state->agentThread);
}
