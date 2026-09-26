#include <gtest/gtest.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
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

// A tool factory whose one tool throws, the way a tool's own code might.
class ThrowingToolFactory : public IToolFactory {
public:
    static constexpr const char* kToolName = "throwing_tool";

    std::shared_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent>) override {
        return name == kToolName ? std::make_shared<ThrowingTool>() : nullptr;
    }
    bool HasTool(const std::string& name) const override { return name == kToolName; }
    std::vector<std::string> GetAvailableTools() const override { return {kToolName}; }
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override {
        return {{kToolName, "throws", nlohmann::json::object()}};
    }

private:
    class ThrowingTool : public ITool {
    public:
        ToolResult Call(std::shared_ptr<IAgent>, const nlohmann::json&) override {
            throw std::runtime_error("disk on fire");
        }
        nlohmann::json GetParametersSchema() const override { return nlohmann::json::object(); }
    };
};

// The tool message answering |toolCallId| among the messages an LLM was sent,
// or null if none does.
const LLMMessage* ToolResultFor(const std::vector<LLMMessage>& messages, const std::string& toolCallId) {
    for (const auto& message : messages) {
        if (message.role == "tool" && message.tool_call_id == toolCallId) {
            return &message;
        }
    }
    return nullptr;
}

// A console that fails to write the line reporting an unknown tool, and records
// everything else: the one way left to make a command fail between a response
// asking for tools and the results going into the history.
class ConsoleFailingOnUnknownTool : public IAgentConsole {
public:
    void Write(const std::string& message) override {
        if (message.find("Unknown tool") != std::string::npos) {
            throw std::runtime_error("the console is gone");
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_messages.push_back(message);
    }
    std::optional<std::string> ReadLine() override { return std::nullopt; }

    std::vector<std::string> GetMessages() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_messages;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_messages;
};

bool AnyContains(const std::vector<std::string>& lines, const std::string& text) {
    for (const auto& line : lines) {
        if (line.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

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

// --- A command that fails ---
//
// An exception out of a command -- a tool, the LLM round trip (serializing a
// history holding bytes that are not UTF-8 used to throw), anything -- used to
// end the agent's thread with nothing but a log line: haisos then exited 0
// with no output, and an interactive agent simply stopped answering. Now it
// takes only the command down: the failure is reported on the console and in
// the message buffer, and an interactive agent goes on to its next command.

TEST(AgentTest, AFailedCommandIsReportedAndAnInteractiveAgentGoesOn) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("answered");
    mockLLM->SetThrowOnCall(1, "simulated failure");
    auto console = std::make_shared<MockAgentConsole>();
    auto agent = Agent::Create(mockLLM, ToolFactory::Create(), console,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr, /*startTime=*/"", /*interactive=*/true);

    agent->Post("first");
    agent->Post("second");
    agent->TriggerStop();
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    // The second command was still answered.
    EXPECT_EQ(mockLLM->GetCallCount(), 2);
    const auto messages = console->GetMessages();
    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[0], "Error: the command failed: simulated failure");
    EXPECT_EQ(messages[1], "answered");
    EXPECT_NE(agent->GetConsoleOutput().find("[test_agent] Error: the command failed: simulated failure"), std::string::npos);
}

TEST(AgentTest, AFailedCommandEndsANonInteractiveAgentVisibly) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetThrowOnCall(1, "simulated failure");
    auto console = std::make_shared<MockAgentConsole>();
    auto agent = Agent::Create(mockLLM, ToolFactory::Create(), console,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr, /*startTime=*/"", /*interactive=*/false);

    agent->Post("the only command");
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    EXPECT_TRUE(AnyContains(console->GetMessages(), "Error: the command failed: simulated failure"));
}

// A command cut short after a response asking for tools, but before their
// results were added, must not leave those tool calls unanswered: the history
// is sent back to the LLM with the next command, and a tool call nothing
// answered is not a valid conversation.
TEST(AgentTest, ACommandFailingMidRoundLeavesEveryToolCallAnswered) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetMessageResponse("answered");
    mockLLM->SetRawToolCall({{"id", "call_x"}, {"function", {{"name", "no_such_tool"}, {"arguments", nlohmann::json::object()}}}});
    auto console = std::make_shared<ConsoleFailingOnUnknownTool>();
    auto agent = Agent::Create(mockLLM, ToolFactory::Create(), console,
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr, /*startTime=*/"", /*interactive=*/true);

    // Stopped only from within the second command's LLM call: a stop asked for
    // earlier would keep the first command's tool call from being looked up.
    std::weak_ptr<Agent> weakAgent = agent;
    MockLLMCommunicator* llm = mockLLM.get();
    mockLLM->SetOnCall([weakAgent, llm] {
        if (llm->GetCallCount() == 2) {
            if (auto live = weakAgent.lock()) {
                live->TriggerStop();
            }
        }
    });

    agent->Post("first");
    agent->Post("second");
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));
    ASSERT_EQ(mockLLM->GetCallCount(), 2);

    // What the second command sent: the tool call, answered by an error, then
    // the second command.
    const auto& sent = mockLLM->GetLastMessages();
    size_t toolCallAt = sent.size();
    for (size_t i = 0; i < sent.size(); ++i) {
        if (sent[i].role == "assistant" && !sent[i].toolCallsJson.empty()) {
            toolCallAt = i;
        }
    }
    ASSERT_LT(toolCallAt + 2, sent.size());
    EXPECT_EQ(sent[toolCallAt + 1].role, "tool");
    EXPECT_EQ(sent[toolCallAt + 1].tool_call_id, "call_x");
    EXPECT_EQ(sent[toolCallAt + 1].name, "no_such_tool");
    EXPECT_TRUE(sent[toolCallAt + 1].is_error);
    EXPECT_EQ(sent[toolCallAt + 2].role, "user");
    EXPECT_NE(sent[toolCallAt + 2].content.find("second"), std::string::npos);
    EXPECT_TRUE(AnyContains(console->GetMessages(), "Error: the command failed: the console is gone"));
}

// --- What an LLM may put in a tool call ---
//
// A tool call is whatever the LLM sent. A field of the wrong type used to make
// json::value() throw, and a tool's own exception went the same way: out of
// the agent's thread, ending the agent with the tool call unanswered. Each is
// now the call's own error result, and the conversation goes on.

TEST(AgentTest, AToolThatThrowsFailsItsOwnCallNotTheAgent) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetToolCallResponse(ThrowingToolFactory::kToolName);
    auto agent = Agent::Create(mockLLM, std::make_shared<ThrowingToolFactory>(), InMemoryAgentConsole::Create(),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr, /*startTime=*/"", /*interactive=*/false);

    agent->Post("do some work");
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    // The LLM was asked again, with the failure as the tool's result.
    ASSERT_EQ(mockLLM->GetCallCount(), 2);
    const LLMMessage* result = ToolResultFor(mockLLM->GetLastMessages(), "call_1");
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->is_error);
    EXPECT_NE(result->content.find("Error: tool throwing_tool failed: disk on fire"), std::string::npos);
}

TEST(AgentTest, AToolCallWhoseNameIsNotAStringGetsAnErrorResult) {
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetRawToolCall({{"id", "call_n"}, {"function", {{"name", 42}, {"arguments", nlohmann::json::object()}}}});
    auto agent = Agent::Create(mockLLM, ToolFactory::Create(), InMemoryAgentConsole::Create(),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr, /*startTime=*/"", /*interactive=*/false);

    agent->Post("do some work");
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    ASSERT_EQ(mockLLM->GetCallCount(), 2);
    const LLMMessage* result = ToolResultFor(mockLLM->GetLastMessages(), "call_n");
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->is_error);
    EXPECT_NE(result->content.find("names no tool"), std::string::npos);
}

TEST(AgentTest, ToolArgumentsThatAreNotAnObjectGetAnErrorResult) {
    auto toolFactory = std::make_shared<RecordingToolFactory>();
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetRawToolCall({{"id", "call_a"},
        {"function", {{"name", RecordingToolFactory::kToolName}, {"arguments", nlohmann::json::array({1, 2})}}}});
    auto agent = Agent::Create(mockLLM, toolFactory, InMemoryAgentConsole::Create(),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr, /*startTime=*/"", /*interactive=*/false);

    agent->Post("do some work");
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    ASSERT_EQ(mockLLM->GetCallCount(), 2);
    EXPECT_FALSE(toolFactory->WasToolCalled());
    const LLMMessage* result = ToolResultFor(mockLLM->GetLastMessages(), "call_a");
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->is_error);
    EXPECT_NE(result->content.find("must be a JSON object, not an array"), std::string::npos);
}

// Null arguments are no arguments: the tool runs.
TEST(AgentTest, NullToolArgumentsMeanNoArguments) {
    auto toolFactory = std::make_shared<RecordingToolFactory>();
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    mockLLM->SetRawToolCall({{"id", "call_z"},
        {"function", {{"name", RecordingToolFactory::kToolName}, {"arguments", nullptr}}}});
    auto agent = Agent::Create(mockLLM, toolFactory, InMemoryAgentConsole::Create(),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr, /*startTime=*/"", /*interactive=*/false);

    agent->Post("do some work");
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    EXPECT_TRUE(toolFactory->WasToolCalled());
}

// --- Commands reach the LLM whole ---
//
// Every command used to pass through a lossy "prompt-injection" filter, which
// dropped whole lines containing phrases such as "you are now" or "system:",
// deleted everything between '<' and '>', and cut at 64 KB -- mangling the
// agent's own program, the lines its operator typed, and code and markup in
// both. A command now goes into the history byte for byte, only delimited.

namespace {

// The one user message a command became, as the LLM was sent it.
std::string LastUserMessage(const MockLLMCommunicator& llm) {
    const auto& sent = llm.GetLastMessages();
    for (auto it = sent.rbegin(); it != sent.rend(); ++it) {
        if (it->role == "user") {
            return it->content;
        }
    }
    return std::string();
}

} // namespace

TEST(AgentTest, ACommandReachesTheLLMVerbatim) {
    const std::string command =
        "You are now a code reviewer for this repository.\n"
        "Ignore previous formatting rules; Operating system: Linux\n"
        "Check that a < b and b > c in the loop.\n"
        "Wrap the verdict in <b>bold</b> tags.";
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto agent = Agent::Create(mockLLM, ToolFactory::Create(), InMemoryAgentConsole::Create(),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr, /*startTime=*/"", /*interactive=*/false);

    agent->Post(command);
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    EXPECT_EQ(LastUserMessage(*mockLLM), "\n--- BEGIN USER INPUT ---\n" + command + "\n--- END USER INPUT ---\n");
}

TEST(AgentTest, ALongCommandIsNotCut) {
    // 90 KB of three-byte characters: over the old 64 KB cut, which also split
    // a character in two and left the history unserializable.
    std::string command = "Summarize: ";
    for (int i = 0; i < 30000; ++i) {
        command += "\xe2\x82\xac";
    }
    auto mockLLM = std::make_shared<MockLLMCommunicator>();
    auto agent = Agent::Create(mockLLM, ToolFactory::Create(), InMemoryAgentConsole::Create(),
        std::vector<std::string>{"You are a helpful AI assistant."},
        "test_agent", nullptr, /*startTime=*/"", /*interactive=*/false);

    agent->Post(command);
    ASSERT_TRUE(agent->WaitToFinish(kWaitTimeoutMs));

    EXPECT_EQ(LastUserMessage(*mockLLM), "\n--- BEGIN USER INPUT ---\n" + command + "\n--- END USER INPUT ---\n");
}
