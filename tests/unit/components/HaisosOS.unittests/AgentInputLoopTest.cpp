#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "AgentInputLoop.h"
#include "tests/mocks/MockAgent.h"
#include "tests/mocks/MockFileDescriptor.h"

using namespace Haisos;
using namespace Haisos::Mocks;

namespace {

constexpr uint64_t kWaitMs = 5000;

// An input that counts its reads, so a test can tell whether any happened at
// all. Everything else is MockFileDescriptor's: Feed() hands out bytes (as a
// user typing a line would), EndInput() ends them.
class CountingInput : public MockFileDescriptor {
public:
    ssize_t Read(void* buf, size_t count) override {
        ++m_readCalls;
        return MockFileDescriptor::Read(buf, count);
    }
    int ReadCalls() const { return m_readCalls.load(); }

private:
    std::atomic<int> m_readCalls{0};
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

TEST(AgentInputLoopTest, CreateRefusesAMissingAgentOrInput) {
    EXPECT_EQ(AgentInputLoop::Create(nullptr, std::make_shared<MockFileDescriptor>()), nullptr);
    EXPECT_EQ(AgentInputLoop::Create(std::make_shared<MockAgent>(), nullptr), nullptr);
}

TEST(AgentInputLoopTest, ANotStartedLoopHasNotFinishedAndReadsNothing) {
    auto agent = std::make_shared<MockAgent>();
    auto input = std::make_shared<CountingInput>();
    input->Feed("never read\n");
    auto loop = AgentInputLoop::Create(agent, input);
    ASSERT_NE(loop, nullptr);

    EXPECT_FALSE(loop->WaitToFinish(0));
    EXPECT_EQ(input->ReadCalls(), 0);
}

TEST(AgentInputLoopTest, PostsEveryLineThenStopsTheAgentAtEndOfInput) {
    auto agent = std::make_shared<MockAgent>();
    auto input = std::make_shared<MockFileDescriptor>();
    input->Feed("first\n\nsecond\n");
    input->EndInput();
    auto loop = AgentInputLoop::Create(agent, input);
    loop->Start();

    ASSERT_TRUE(loop->WaitToFinish(kWaitMs));
    EXPECT_EQ(agent->GetCommands(), (std::vector<std::string>{"first", "", "second"}));
    // Nothing more can ever arrive, so the agent is asked to stop rather than
    // left waiting forever.
    EXPECT_TRUE(agent->WasStopTriggered());
}

TEST(AgentInputLoopTest, PostsALastLineWithoutNewline) {
    auto agent = std::make_shared<MockAgent>();
    auto input = std::make_shared<MockFileDescriptor>();
    input->Feed("a\nb");
    input->EndInput();
    auto loop = AgentInputLoop::Create(agent, input);
    loop->Start();

    ASSERT_TRUE(loop->WaitToFinish(kWaitMs));
    EXPECT_EQ(agent->GetCommands(), (std::vector<std::string>{"a", "b"}));
}

TEST(AgentInputLoopTest, StripsCarriageReturns) {
    auto agent = std::make_shared<MockAgent>();
    auto input = std::make_shared<MockFileDescriptor>();
    input->Feed("x\r\n");
    input->EndInput();
    auto loop = AgentInputLoop::Create(agent, input);
    loop->Start();

    ASSERT_TRUE(loop->WaitToFinish(kWaitMs));
    EXPECT_EQ(agent->GetCommands(), (std::vector<std::string>{"x"}));
}

TEST(AgentInputLoopTest, DoesNotReadForAnAgentThatHasAlreadyClosed) {
    auto agent = std::make_shared<MockAgent>();
    agent->SetFinished(true);
    auto input = std::make_shared<CountingInput>();
    input->Feed("never read\n");
    auto loop = AgentInputLoop::Create(agent, input);
    loop->Start();

    ASSERT_TRUE(loop->WaitToFinish(kWaitMs));
    EXPECT_EQ(input->ReadCalls(), 0);
    EXPECT_TRUE(agent->GetCommands().empty());
}

TEST(AgentInputLoopTest, ALineTypedAfterTheAgentClosedEndsTheLoopWithoutBeingPosted) {
    auto agent = std::make_shared<MockAgent>();
    auto input = std::make_shared<MockFileDescriptor>();
    auto loop = AgentInputLoop::Create(agent, input);
    loop->Start();

    input->Feed("hello\n");
    ASSERT_TRUE(WaitUntil([&] { return agent->GetCommands().size() == 1; }));

    // The agent closes itself (self_close) while the loop waits for a line:
    // the loop cannot know until the next line arrives.
    agent->SetFinished(true);
    EXPECT_FALSE(loop->WaitToFinish(50));

    input->Feed("too late\n");
    ASSERT_TRUE(loop->WaitToFinish(kWaitMs));
    EXPECT_EQ(agent->GetCommands(), (std::vector<std::string>{"hello"}));
    // It closed by itself; there was nothing to ask it.
    EXPECT_FALSE(agent->WasStopTriggered());
}
