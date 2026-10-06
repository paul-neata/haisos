#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "HaisosOS.h"
#include "ProcessFileIO.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/libheaders/ExitCodes.h"
#include "Factory.h"
#include "ServicesCreator.h"

using namespace Haisos;

namespace {

// 127.0.0.1, not localhost -- as in HaisosOSTest's kUnreachableEndpoint: a
// refused connection there fails the agent's LLM call at once.
const std::string kUnreachableEndpoint = "http://127.0.0.1:9999/api/chat";
// Generous: only bounds a hang; every process in these tests ends promptly.
constexpr uint64_t kProcessWaitMs = 30000;

// A physical console that captures what is written to it (stdout and stderr
// kept apart) and has no input.
class CapturingPhysicalConsole : public IPhysicalConsole {
public:
    void Write(const std::string& bytes) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_written += bytes;
    }
    void WriteError(const std::string& bytes) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_writtenError += bytes;
    }
    std::optional<std::string> ReadLine() override { return std::nullopt; }
    void Start() override {}
    void Stop() override {}

    std::string Written() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_written;
    }
    std::string WrittenError() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_writtenError;
    }

private:
    mutable std::mutex m_mutex;
    std::string m_written;
    std::string m_writtenError;
};

// Reads through descriptor until it reports end of file; ASSERTs on failure.
std::string ReadToEndOfFile(IFileDescriptor* descriptor) {
    std::string all;
    char buf[65536];
    for (;;) {
        const ssize_t n = descriptor->Read(buf, sizeof(buf));
        if (n == 0) {
            return all;
        }
        EXPECT_GT(n, 0);
        if (n <= 0) {
            return all;
        }
        all.append(buf, static_cast<size_t>(n));
    }
}

} // namespace

class HaisosOSPipeTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_console = std::make_shared<CapturingPhysicalConsole>();
        m_builtinCommands = m_factory->CreateBuiltinCommands();
        auto servicesCreator = CreateServicesCreator();
        auto root = servicesCreator->CreateFileSystemService()->CreateEmptyInMemFileSystem();
        ASSERT_EQ(root->CreateDirectory("/bin", 0), 0);
        auto configurator = m_factory->CreateBuiltinConfigurator();
        for (const auto& name : m_builtinCommands->GetCommands()) {
            ASSERT_TRUE(configurator->AddBuiltinCommand(root, "/bin/" + name, name)) << name;
        }
        WriteFile(root, "/a.md", "Say hello.");
        m_root = root;
        m_os = m_factory->CreateHaisosOS(
            servicesCreator, m_console, root, m_builtinCommands, TestEnvironment());
        ASSERT_NE(m_os, nullptr);
    }

    std::shared_ptr<IEnvironment> TestEnvironment() {
        auto environment = m_factory->CreateEnvironment();
        environment->SetVariable(kEnvEndpoint, kUnreachableEndpoint);
        environment->SetVariable(kEnvModel, "llama3");
        return environment;
    }

    static void WriteFile(const std::shared_ptr<IFileSystem>& fs, const std::string& path, const std::string& content) {
        auto file = fs->OpenFile(path, kFileOpenWriteCreateTruncate, 0);
        ASSERT_NE(file, nullptr);
        ASSERT_EQ(file->Write(content.data(), content.size()), static_cast<ssize_t>(content.size()));
    }

    std::shared_ptr<IFactory> m_factory = CreateFactory();
    std::shared_ptr<CapturingPhysicalConsole> m_console;
    std::shared_ptr<IBuiltinCommands> m_builtinCommands;
    std::shared_ptr<IFileSystem> m_root;
    std::shared_ptr<IHaisosOS> m_os;
};

TEST_F(HaisosOSPipeTest, EachOSHasAPipeServiceOfItsOwn) {
    auto service = m_os->GetPipeService();
    ASSERT_NE(service, nullptr);
    EXPECT_EQ(m_os->GetPipeService(), service);

    auto subOS = m_os->CreateSubOS(
        m_os->GetServicesCreator()->Clone(), m_console, m_root, m_builtinCommands, m_os->GetOsEnvironment()->Clone());
    ASSERT_NE(subOS, nullptr);
    ASSERT_NE(subOS->GetPipeService(), nullptr);
    EXPECT_NE(subOS->GetPipeService(), service);
}

TEST_F(HaisosOSPipeTest, CreatePipeTakesTheTwoLowestFreeSlots) {
    auto io = ProcessFileIO::Create(m_os, "/");

    // Three placeholders: a pipe's two ends and a dup -- slots 0, 1 and 2.
    auto ends = m_os->GetPipeService()->CreatePipe();
    ASSERT_EQ(io->AddDescriptor(ends.readEnd), 0);
    ASSERT_EQ(io->AddDescriptor(ends.writeEnd), 1);
    ASSERT_EQ(io->Dup(0), 2);
    ASSERT_EQ(io->CloseDescriptor(1), 0);

    const size_t openBefore = m_os->GetPipeService()->OpenPipeCount();
    auto slots = io->CreatePipe();
    ASSERT_TRUE(slots.has_value());
    // The two lowest free slots, read end first.
    EXPECT_EQ(slots->first, 1);
    EXPECT_EQ(slots->second, 3);
    EXPECT_EQ(m_os->GetPipeService()->OpenPipeCount(), openBefore + 1);

    io->ReleaseAllDescriptors();
}

TEST_F(HaisosOSPipeTest, CreatePipeMovesBytesFromTheWriteSlotToTheReadSlot) {
    auto io = ProcessFileIO::Create(m_os, "/");
    const size_t openBefore = m_os->GetPipeService()->OpenPipeCount();
    auto slots = io->CreatePipe();
    ASSERT_TRUE(slots.has_value());
    const auto& [readSlot, writeSlot] = *slots;

    auto writer = io->GetDescriptor(writeSlot);
    auto reader = io->GetDescriptor(readSlot);
    ASSERT_NE(writer, nullptr);
    ASSERT_NE(reader, nullptr);
    EXPECT_EQ(writer->Write("hello", 5), 5);
    char buf[8] = {};
    EXPECT_EQ(reader->Read(buf, sizeof(buf)), 5);
    EXPECT_EQ(std::string(buf, 5), "hello");

    // The write slot closed, and the test's own copy let go: end of file at
    // the read slot.
    EXPECT_EQ(io->CloseDescriptor(writeSlot), 0);
    writer.reset();
    EXPECT_EQ(reader->Read(buf, sizeof(buf)), 0);
    EXPECT_EQ(io->CloseDescriptor(readSlot), 0);
    reader.reset();
    EXPECT_EQ(m_os->GetPipeService()->OpenPipeCount(), openBefore);
    io->ReleaseAllDescriptors();
}

TEST_F(HaisosOSPipeTest, CreatePipeFailsWhenFewerThanTwoSlotsAreFree) {
    auto io = ProcessFileIO::Create(m_os, "/");
    const size_t openBefore = m_os->GetPipeService()->OpenPipeCount();

    // Fill the table to one slot short of full: one AddDescriptor, then Dups.
    auto ends = m_os->GetPipeService()->CreatePipe();
    ASSERT_EQ(io->AddDescriptor(ends.readEnd), 0);
    ends.readEnd.reset();
    // The write end would keep this pipe counted after ReleaseAllDescriptors
    // below; only the table is meant to hold it.
    ends.writeEnd.reset();
    for (int dups = 0; dups < IFileIO::kMaxDescriptors - 2; ++dups) {
        ASSERT_EQ(io->Dup(0), dups + 1);
    }
    // One slot free is not enough for a pipe; the table is left as it was,
    // the failed pipe closed with its ends.
    EXPECT_EQ(io->CreatePipe(), std::nullopt);
    EXPECT_EQ(m_os->GetPipeService()->OpenPipeCount(), openBefore + 1);
    // The one free slot was given back untouched.
    EXPECT_EQ(io->Dup(0), IFileIO::kMaxDescriptors - 1);
    EXPECT_EQ(io->CreatePipe(), std::nullopt);
    EXPECT_EQ(m_os->GetPipeService()->OpenPipeCount(), openBefore + 1);

    io->ReleaseAllDescriptors();
    EXPECT_EQ(m_os->GetPipeService()->OpenPipeCount(), openBefore);
}

TEST_F(HaisosOSPipeTest, CreatePipeWithoutAnOSFails) {
    auto io = ProcessFileIO::Create(std::weak_ptr<IHaisosOS>(), "/");
    EXPECT_EQ(io->CreatePipe(), std::nullopt);
}

TEST_F(HaisosOSPipeTest, AReaderSeesEndOfFileWhenTheWritersProgramEnds) {
    auto ends = m_os->GetPipeService()->CreatePipe();
    StartProcessOptions options;
    options.stdOut = ends.writeEnd;
    auto process = m_os->StartProcess(TestEnvironment(), "/bin/echo", {"hi"}, "", options);
    ASSERT_NE(process, nullptr);
    // Every copy of the write end the test holds goes: the process's slot 1
    // is the only writer left, so the stream ends when its descriptors are
    // released.
    ends.writeEnd.reset();
    options.stdOut.reset();

    EXPECT_EQ(ReadToEndOfFile(ends.readEnd.get()), "hi\n");
    // The read can return 0 a few instructions before the process reports
    // finished -- its descriptors are released before that.
    EXPECT_TRUE(process->WaitToFinish(kProcessWaitMs));
    EXPECT_EQ(process->ExitCode(), std::optional<int>(0));
}

TEST_F(HaisosOSPipeTest, ABuiltinBlockedOnAFullPipeStopsWhenAskedTo) {
    auto ends = m_os->GetPipeService()->CreatePipe();
    StartProcessOptions options;
    options.stdOut = ends.writeEnd;
    // 100000 'x': more than the 64 KiB the pipe holds, so the command blocks
    // on the full pipe once 65536 bytes are in. The test keeps the read end
    // and never drains while the command runs.
    const std::string huge(100000, 'x');
    auto process = m_os->StartProcess(TestEnvironment(), "/bin/echo", {huge}, "", options);
    ASSERT_NE(process, nullptr);
    ends.writeEnd.reset();
    options.stdOut.reset();

    EXPECT_FALSE(process->WaitToFinish(200));
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(5000));
    EXPECT_EQ(process->ExitCode(), std::optional<int>(kExitCodeStopped));

    EXPECT_EQ(ReadToEndOfFile(ends.readEnd.get()), std::string(kDefaultPipeCapacity, 'x'));
}

TEST_F(HaisosOSPipeTest, AStopOfAnInteractiveAgentInterruptsItsPipedStdin) {
    auto ends = m_os->GetPipeService()->CreatePipe();
    // Released on every path: left open, a failed assertion would leave the
    // input loop blocked on the silent pipe and the test would hang in
    // teardown.
    struct ReleaseWriteEnd {
        std::shared_ptr<IFileDescriptor>& end;
        ~ReleaseWriteEnd() { end.reset(); }
    } guard{ends.writeEnd};

    StartProcessOptions options;
    options.interactive = true;
    options.stdIn = ends.readEnd;
    ends.readEnd.reset(); // the process's slot 0 (and options) hold it now
    auto process = m_os->StartProcess(TestEnvironment(), "/a.md", {}, "", options);
    ASSERT_NE(process, nullptr);

    // The LLM call fails at once against the unreachable endpoint; the
    // interactive agent then waits for input, and the input loop is blocked
    // reading the silent pipe. Asking to stop must interrupt that read.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(5000));
}
