#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "AgentInputLoop.h"
#include "src/components/libheaders/SynchronizedQueue.h"
#include "tests/mocks/MockAgent.h"
#include "tests/mocks/MockAgentConsole.h"

using namespace Haisos;
using namespace Haisos::Mocks;

namespace {

constexpr uint64_t kWaitMs = 5000;

// A console whose ReadLine blocks until the test hands it a line (or end of
// input, as nullopt), like a user who has not typed anything yet.
class BlockingConsole : public IAgentConsole {
public:
    void Write(const std::string&) override {}
    std::optional<std::string> ReadLine() override {
        std::optional<std::string> line;
        if (!m_lines.Pop(line)) {
            return std::nullopt;
        }
        return line;
    }
    void Type(std::optional<std::string> line) { m_lines.Post(std::move(line)); }

private:
    SynchronizedQueue<std::optional<std::string>> m_lines;
};

template <typename Predicate>
bool WaitUntil(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kWaitMs);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

} // namespace

TEST(AgentInputLoopTest, CreateRefusesAMissingAgentOrConsole) {
    EXPECT_EQ(AgentInputLoop::Create(nullptr, std::make_shared<MockAgentConsole>()), nullptr);
    EXPECT_EQ(AgentInputLoop::Create(std::make_shared<MockAgent>(), nullptr), nullptr);
}

TEST(AgentInputLoopTest, ANotStartedLoopHasNotFinishedAndReadsNothing) {
    auto agent = std::make_shared<MockAgent>();
    auto console = std::make_shared<MockAgentConsole>();
    auto loop = AgentInputLoop::Create(agent, console);
    ASSERT_NE(loop, nullptr);

    EXPECT_FALSE(loop->WaitToFinish(0));
    EXPECT_EQ(console->GetReadLineCalls(), 0);
}

TEST(AgentInputLoopTest, PostsEveryLineThenStopsTheAgentAtEndOfInput) {
    auto agent = std::make_shared<MockAgent>();
    auto console = std::make_shared<MockAgentConsole>();
    console->SetInputLines({"first", "", "second"});
    auto loop = AgentInputLoop::Create(agent, console);
    loop->Start();

    ASSERT_TRUE(loop->WaitToFinish(kWaitMs));
    EXPECT_EQ(agent->GetCommands(), (std::vector<std::string>{"first", "", "second"}));
    // Nothing more can ever arrive, so the agent is asked to stop rather than
    // left waiting forever.
    EXPECT_TRUE(agent->WasStopTriggered());
}

TEST(AgentInputLoopTest, DoesNotReadForAnAgentThatHasAlreadyClosed) {
    auto agent = std::make_shared<MockAgent>();
    agent->SetFinished(true);
    auto console = std::make_shared<MockAgentConsole>();
    console->SetInputLines({"never read"});
    auto loop = AgentInputLoop::Create(agent, console);
    loop->Start();

    ASSERT_TRUE(loop->WaitToFinish(kWaitMs));
    EXPECT_EQ(console->GetReadLineCalls(), 0);
    EXPECT_TRUE(agent->GetCommands().empty());
}

TEST(AgentInputLoopTest, ALineTypedAfterTheAgentClosedEndsTheLoopWithoutBeingPosted) {
    auto agent = std::make_shared<MockAgent>();
    auto console = std::make_shared<BlockingConsole>();
    auto loop = AgentInputLoop::Create(agent, console);
    loop->Start();

    console->Type(std::string("hello"));
    ASSERT_TRUE(WaitUntil([&] { return agent->GetCommands().size() == 1; }));

    // The agent closes itself (self_close) while the loop waits for a line:
    // the loop cannot know until the next line arrives.
    agent->SetFinished(true);
    EXPECT_FALSE(loop->WaitToFinish(50));

    console->Type(std::string("too late"));
    ASSERT_TRUE(loop->WaitToFinish(kWaitMs));
    EXPECT_EQ(agent->GetCommands(), (std::vector<std::string>{"hello"}));
    // It closed by itself; there was nothing to ask it.
    EXPECT_FALSE(agent->WasStopTriggered());
}
