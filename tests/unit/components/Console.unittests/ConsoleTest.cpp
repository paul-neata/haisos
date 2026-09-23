#include <gtest/gtest.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include "Console.h"
#include "AgentConsoleAdapter.h"
#include "src/components/Logger/Logger.h"

using namespace Haisos;

namespace {

// Records every Write() call verbatim, so tests can assert on exactly what
// was forwarded (and, in particular, catch double-tagging).
class RecordingPhysicalConsole : public IPhysicalConsole {
public:
    void Write(const std::string& message) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_calls.push_back(message);
    }
    void Start() override {}
    void Stop() override {}

    std::vector<std::string> GetCalls() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_calls;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_calls;
};

}

TEST(ConsoleTest, Construction) {
    auto console = Console::Create();
    // Should not crash
}

TEST(ConsoleTest, WriteWithoutStartDoesNotCrash) {
    auto console = Console::Create();
    console->Write("test message");
    // Messages are queued but not processed until Start()
}

TEST(ConsoleTest, StartStopIdempotent) {
    auto console = Console::Create();
    console->Start();
    console->Start(); // second start should be no-op
    console->Stop();
    console->Stop(); // second stop should be no-op
}

TEST(ConsoleTest, ProcessesMessages) {
    auto console = Console::Create();
    console->Start();
    console->Write("message 1");
    console->Write("message 2");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    console->Stop();
}

TEST(AgentConsoleAdapterTest, TagsWithItsSourceNameExactlyOnce) {
    auto physical = std::make_shared<RecordingPhysicalConsole>();
    auto adapter = AgentConsoleAdapter::Create(physical, "my_process");

    adapter->Write("hello");

    // The adapter is the only thing that tags: the console writes what it is
    // given. So exactly one "[my_process] " turns up, and a caller (e.g. Agent)
    // that had also prefixed the name would show as double-tagging here.
    auto calls = physical->GetCalls();
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0], "[my_process] hello");
}

TEST(ConsoleTest, IsNotALogReceiver) {
    LogClearMessageReceivers();

    auto console = Console::Create();
    console->Start();

    // Nothing the Logger emits may reach the program's own output: where the
    // log goes is set on the Logger, never by creating a console.
    LogInfo("Test log message");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    console->Stop();
}
