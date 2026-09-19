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
        m_calls.emplace_back("", message);
    }
    void Write(const std::string& sourceName, const std::string& message) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_calls.emplace_back(sourceName, message);
    }
    void Start() override {}
    void Stop() override {}

    std::vector<std::pair<std::string, std::string>> GetCalls() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_calls;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::pair<std::string, std::string>> m_calls;
};

}

TEST(ConsoleTest, Construction) {
    auto console = Console::Create(false);
    // Should not crash
}

TEST(ConsoleTest, WriteWithoutStartDoesNotCrash) {
    auto console = Console::Create(false);
    console->Write("test message");
    // Messages are queued but not processed until Start()
}

TEST(ConsoleTest, StartStopIdempotent) {
    auto console = Console::Create(false);
    console->Start();
    console->Start(); // second start should be no-op
    console->Stop();
    console->Stop(); // second stop should be no-op
}

TEST(ConsoleTest, ProcessesMessages) {
    auto console = Console::Create(false);
    console->Start();
    console->Write("message 1");
    console->Write("message 2");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    console->Stop();
}

TEST(ConsoleTest, RegisterAsLogReceiverReceivesMessages) {
    LogClearMessageReceivers();

    {
        auto console = Console::Create(true);
        console->Start();

        LogInfo("Test log message");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        console->Stop();
    }

    // After console is destroyed, it should unregister itself
    // Logging should still work (go to stderr)
    LogInfo("Post-console log message");
}

TEST(ConsoleTest, MultipleConsolesRegisterIndependentReceivers) {
    LogClearMessageReceivers();

    auto console1 = Console::Create(true);
    auto console2 = Console::Create(true);

    console1->Start();
    console2->Start();

    LogInfo("Message for both consoles");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    console1->Stop();
    console2->Stop();

    // After both consoles are stopped, log receivers should be empty
}

TEST(AgentConsoleAdapterTest, ForwardsWriteWithSourceNameExactlyOnceUntagged) {
    auto physical = std::make_shared<RecordingPhysicalConsole>();
    auto adapter = AgentConsoleAdapter::Create(physical, "my_process");

    adapter->Write("hello");

    auto calls = physical->GetCalls();
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0].first, "my_process");
    // The message handed to the physical console must be untagged: tagging by
    // source is the physical console's own job (see Console::Write), so a
    // caller (e.g. Agent) must not have already prefixed "[my_process] " onto
    // it, or output would end up double-tagged.
    EXPECT_EQ(calls[0].second, "hello");
}
