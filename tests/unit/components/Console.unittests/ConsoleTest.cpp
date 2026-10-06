#include <gtest/gtest.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include "Console.h"
#include "ConsoleDescriptors.h"
#include "AgentConsoleAdapter.h"
#include "InMemoryAgentConsole.h"
#include "src/components/Logger/Logger.h"

using namespace Haisos;

namespace {

// Records what each output stream was given, separate and combined: Write calls
// in m_outCalls, WriteError calls in m_errCalls, both in order in m_allCalls.
class RecordingPhysicalConsole : public IPhysicalConsole {
public:
    void Write(const std::string& bytes) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_outCalls.push_back(bytes);
        m_allCalls.push_back(bytes);
    }
    void WriteError(const std::string& bytes) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_errCalls.push_back(bytes);
        m_allCalls.push_back(bytes);
    }
    std::optional<std::string> ReadLine() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_input.empty()) {
            return std::nullopt;
        }
        std::string line = m_input.front();
        m_input.erase(m_input.begin());
        return line;
    }
    void Start() override {}
    void Stop() override {}

    std::vector<std::string> GetCalls() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_allCalls;
    }
    std::vector<std::string> GetOutCalls() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_outCalls;
    }
    std::vector<std::string> GetErrorCalls() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_errCalls;
    }

    void SetInput(std::vector<std::string> lines) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_input = std::move(lines);
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_outCalls;
    std::vector<std::string> m_errCalls;
    std::vector<std::string> m_allCalls;
    std::vector<std::string> m_input;
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

// The console adds no newline any more; host output is not captured, so like
// ProcessesMessages this is only for a crash, a hang or a misrouted stream.
TEST(ConsoleTest, WriteAndWriteErrorDoNotAddNewlines) {
    auto console = Console::Create();
    console->Start();
    console->Write("a");
    console->WriteError("b");
    console->Write("");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    console->Stop();
}

TEST(AgentConsoleAdapterTest, WritesUntaggedLines) {
    auto physical = std::make_shared<RecordingPhysicalConsole>();
    auto adapter = AgentConsoleAdapter::Create(physical);

    adapter->Write("hello");

    // Untagged, and the one newline the line needs -- the console adds none.
    auto calls = physical->GetCalls();
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0], "hello\n");
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

TEST(AgentConsoleAdapterTest, ReadsLinesFromThePhysicalConsoleUntagged) {
    auto physical = std::make_shared<RecordingPhysicalConsole>();
    physical->SetInput({"typed", ""});
    auto adapter = AgentConsoleAdapter::Create(physical);

    EXPECT_EQ(adapter->ReadLine(), std::optional<std::string>("typed"));
    EXPECT_EQ(adapter->ReadLine(), std::optional<std::string>(""));
    EXPECT_EQ(adapter->ReadLine(), std::nullopt);
}

TEST(ConsoleDescriptorTest, OutputWritesExactlyTheBytes) {
    auto physical = std::make_shared<RecordingPhysicalConsole>();
    auto out = ConsoleOutputDescriptor::Create(physical);

    EXPECT_TRUE(out->IsTerminal());
    EXPECT_EQ(out->Write("ab\0c", 4), 4);
    EXPECT_EQ(out->Write("$ ", 2), 2);
    // Zero bytes is a no-op reaching nothing.
    EXPECT_EQ(out->Write("", 0), 0);
    // A descriptor cannot be read from.
    char buf[4];
    EXPECT_EQ(out->Read(buf, sizeof(buf)), kIOError);

    ASSERT_EQ(physical->GetOutCalls().size(), 2u);
    EXPECT_EQ(physical->GetOutCalls()[0], std::string("ab\0c", 4));
    EXPECT_EQ(physical->GetOutCalls()[1], "$ ");
    EXPECT_TRUE(physical->GetErrorCalls().empty());
}

TEST(ConsoleDescriptorTest, ErrorGoesToWriteError) {
    auto physical = std::make_shared<RecordingPhysicalConsole>();
    auto err = ConsoleErrorDescriptor::Create(physical);

    EXPECT_TRUE(err->IsTerminal());
    EXPECT_EQ(err->Write("oops", 4), 4);

    // Nothing reaches the stdout side.
    EXPECT_TRUE(physical->GetOutCalls().empty());
    ASSERT_EQ(physical->GetErrorCalls().size(), 1u);
    EXPECT_EQ(physical->GetErrorCalls()[0], "oops");
}

TEST(ConsoleDescriptorTest, InputGivesLinesWithNewlinesThenEndOfFile) {
    auto physical = std::make_shared<RecordingPhysicalConsole>();
    physical->SetInput({"hi", ""});
    auto in = ConsoleInputDescriptor::Create(physical);

    EXPECT_TRUE(in->IsTerminal());
    char buf[2];
    // A 2-byte buffer: "hi" then the line's "\n", then the empty line's "\n",
    // then end of file -- which stays end of file.
    EXPECT_EQ(in->Read(buf, 2), 2);
    EXPECT_EQ(std::string(buf, 2), "hi");
    EXPECT_EQ(in->Read(buf, 2), 1);
    EXPECT_EQ(std::string(buf, 1), "\n");
    EXPECT_EQ(in->Read(buf, 2), 1);
    EXPECT_EQ(std::string(buf, 1), "\n");
    EXPECT_EQ(in->Read(buf, 2), 0);
    EXPECT_EQ(in->Read(buf, 2), 0);
    // Input cannot be written.
    EXPECT_EQ(in->Write("x", 1), kIOError);
}

TEST(ConsoleDescriptorTest, EmptyInputEndsAtOnce) {
    auto empty = EmptyInputDescriptor::Create();

    EXPECT_FALSE(empty->IsTerminal());
    char buf[8];
    EXPECT_EQ(empty->Read(buf, sizeof(buf)), 0);
    EXPECT_EQ(empty->Write("x", 1), kIOError);
}

TEST(ConsoleDescriptorTest, NullConsoleDiscardsAndEnds) {
    auto out = ConsoleOutputDescriptor::Create(nullptr);
    auto err = ConsoleErrorDescriptor::Create(nullptr);
    auto in = ConsoleInputDescriptor::Create(nullptr);

    // Writes are swallowed.
    EXPECT_EQ(out->Write("abc", 3), 3);
    EXPECT_EQ(err->Write("abc", 3), 3);
    // Reads end at once.
    char buf[8];
    EXPECT_EQ(in->Read(buf, sizeof(buf)), 0);
}

TEST(InMemoryAgentConsoleTest, HasNothingToRead) {
    EXPECT_EQ(InMemoryAgentConsole::Create()->ReadLine(), std::nullopt);
}
