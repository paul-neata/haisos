#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <nlohmann/json.hpp>
#include "HaisosOS.h"
#include "OSToolFactory.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "Factory.h"
#include "ServicesCreator.h"
#include "tests/mocks/MockFileDescriptor.h"

using namespace Haisos;

namespace {

const std::string kTestRoot = (std::filesystem::temp_directory_path() / "haisos_os_test_root").u8string();
// 127.0.0.1, not localhost: on Windows a refused connection takes about 2 s,
// and localhost tries ::1 first, then 127.0.0.1 -- twice that, for every agent
// test, which put this executable over the test runner's 30 s limit.
const std::string kUnreachableEndpoint = "http://127.0.0.1:9999/api/chat";
// Generous: every LLM call fails soon against the unreachable endpoint, so this
// only bounds a hang.
constexpr uint64_t kProcessWaitMs = 30000;
// The file read_held.lua reads, which a GatedFileSystem root holds the open of.
const std::string kHeldPath = "/held.txt";

// A physical console with scripted input: ReadLine hands out the given lines,
// then reports end of input.
class ScriptedPhysicalConsole : public IPhysicalConsole {
public:
    explicit ScriptedPhysicalConsole(std::vector<std::string> lines) : m_lines(lines.begin(), lines.end()) {}

    void Write(const std::string& bytes) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_written += bytes;
    }
    void WriteError(const std::string& bytes) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_writtenError += bytes;
    }
    std::optional<std::string> ReadLine() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_readLineCalls;
        if (m_lines.empty()) {
            return std::nullopt;
        }
        std::string line = m_lines.front();
        m_lines.pop_front();
        return line;
    }
    void Start() override {}
    void Stop() override {}

    int ReadLineCalls() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_readLineCalls;
    }
    // What stdout, and stderr, were given so far.
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
    std::deque<std::string> m_lines;
    int m_readLineCalls = 0;
    std::string m_written;
    std::string m_writtenError;
};

// A root filesystem that holds every open of one path until Release(), and
// passes everything else straight through to the filesystem it wraps. Holding a
// script's os_read_file there freezes the script inside the tool call (see
// "Objects released last on their own threads" below).
class GatedFileSystem : public IFileSystem {
public:
    GatedFileSystem(std::shared_ptr<IFileSystem> inner, std::string gatedPath)
        : m_inner(std::move(inner)), m_gatedPath(std::move(gatedPath)) {}

    // Whether an open of the gated path has reached the gate within timeoutMs.
    bool WaitUntilHeld(uint64_t timeoutMs) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return m_held; });
    }

    void Release() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_released = true;
        }
        m_cv.notify_all();
    }

    std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags) override {
        HoldIfGated(pathname);
        return m_inner->OpenFile(pathname, flags);
    }
    std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags, int mode) override {
        HoldIfGated(pathname);
        return m_inner->OpenFile(pathname, flags, mode);
    }
    int CreateDirectory(const std::string& pathname, int mode) override { return m_inner->CreateDirectory(pathname, mode); }
    int RemoveDirectory(const std::string& pathname) override { return m_inner->RemoveDirectory(pathname); }
    int RemoveFile(const std::string& pathname) override { return m_inner->RemoveFile(pathname); }
    void Mount(const std::string& whereToMount, std::shared_ptr<IFileSystem> toBeMounted) override {
        m_inner->Mount(whereToMount, std::move(toBeMounted));
    }
    void Unmount(const std::string& mountedPath) override { m_inner->Unmount(mountedPath); }
    std::vector<DirectoryEntry> ReadDirectory(const std::string& path) override { return m_inner->ReadDirectory(path); }
    int Stat(const std::string& path, FileStatus& out) override { return m_inner->Stat(path, out); }
    int AddBuiltinCommand(const std::string& path, const std::string& builtinName) override {
        return m_inner->AddBuiltinCommand(path, builtinName);
    }
    int RemoveBuiltinCommand(const std::string& path) override { return m_inner->RemoveBuiltinCommand(path); }
    std::optional<std::string> IsBuiltinCommand(const std::string& path) override { return m_inner->IsBuiltinCommand(path); }

private:
    void HoldIfGated(const std::string& pathname) {
        if (pathname != m_gatedPath) {
            return;
        }
        std::unique_lock<std::mutex> lock(m_mutex);
        m_held = true;
        m_cv.notify_all();
        // Bounded, so a test that fails before releasing the gate cannot hang.
        m_cv.wait_for(lock, std::chrono::milliseconds(kProcessWaitMs), [this] { return m_released; });
    }

    std::shared_ptr<IFileSystem> m_inner;
    const std::string m_gatedPath;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_held = false;
    bool m_released = false;
};

// A physical console whose ReadLine stays blocked until Release() and then
// reports end of input: an interactive agent fed from it stays alive exactly
// as long as it blocks. Writes are discarded.
class BlockingPhysicalConsole : public IPhysicalConsole {
public:
    void Write(const std::string&) override {}
    void WriteError(const std::string&) override {}
    std::optional<std::string> ReadLine() override {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_released; });
        return std::nullopt;
    }
    void Start() override {}
    void Stop() override {}

    void Release() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_released = true;
        }
        m_cv.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_released = false;
};

// A descriptor that counts its own destructions, so a test can see exactly
// when a process's table lets a file go. Reads end at once; writes take
// everything.
class ReleaseCountingDescriptor : public IFileDescriptor {
public:
    static std::shared_ptr<ReleaseCountingDescriptor> Create(std::shared_ptr<std::atomic<int>> releases) {
        return std::shared_ptr<ReleaseCountingDescriptor>(new ReleaseCountingDescriptor(std::move(releases)));
    }
    ~ReleaseCountingDescriptor() override { ++*m_releases; }

    ssize_t Read(void*, size_t) override { return 0; }
    ssize_t Write(const void*, size_t count) override { return static_cast<ssize_t>(count); }
    bool IsTerminal() const override { return false; }

private:
    explicit ReleaseCountingDescriptor(std::shared_ptr<std::atomic<int>> releases) : m_releases(std::move(releases)) {}

    std::shared_ptr<std::atomic<int>> m_releases;
};

bool HistoryMentions(const nlohmann::json& history, const std::string& text) {
    for (const auto& message : history) {
        if (message.value("role", "") == "user" && message.value("content", "").find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// The whole of a file on the real disk, or "" if there is none.
std::string ReadHostFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

class HaisosOSTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(kTestRoot);
        std::filesystem::create_directories(kTestRoot + "/sub");
        std::ofstream(kTestRoot + "/hello.md") << "Say hello.";
        std::ofstream(kTestRoot + "/sub/inner.md") << "Say hi from inside sub.";
        std::ofstream(kTestRoot + "/not_a_program.txt") << "irrelevant";
        std::ofstream(kTestRoot + "/script.lua") << "os_list_directory({})";
        // Writes through a *relative* path, so where it lands says which
        // directory the tool resolved against.
        std::ofstream(kTestRoot + "/write_relative.lua")
            << "os_write_file({path = 'written.txt', content = 'hi'})";
        std::ofstream(kTestRoot + kHeldPath) << "held";
        std::ofstream(kTestRoot + "/read_held.lua") << "os_read_file({path = '" << kHeldPath << "'})";
    }

    void TearDown() override {
        std::filesystem::remove_all(kTestRoot);
    }

    std::shared_ptr<IEnvironment> TestEnvironment() {
        auto environment = m_factory->CreateEnvironment();
        environment->SetVariable(kEnvEndpoint, kUnreachableEndpoint);
        environment->SetVariable(kEnvModel, "llama3");
        return environment;
    }

    std::shared_ptr<IHaisosOS> BuildOS(std::shared_ptr<IPhysicalConsole> physicalConsole = nullptr,
                                       std::shared_ptr<IFileSystem> rootFileSystem = nullptr) {
        std::shared_ptr<IServicesCreator> servicesCreator = m_factory->CreateServicesCreator();
        if (!rootFileSystem) {
            rootFileSystem = m_factory->CreatePhysicalFileSystem(kTestRoot);
        }
        if (!physicalConsole) {
            physicalConsole = m_factory->CreatePhysicalConsole();
        }
        return m_factory->CreateHaisosOS(
            std::move(servicesCreator), physicalConsole, rootFileSystem, m_factory->CreateBuiltinCommands(), TestEnvironment());
    }

    std::shared_ptr<IFactory> m_factory = CreateFactory();
};

TEST_F(HaisosOSTest, StartProcessAssignsUniqueTopLevelPids) {
    auto os = BuildOS();

    auto p1 = os->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"", StartProcessOptions{});
    auto p2 = os->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"", StartProcessOptions{});

    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_NE(p1->GetPid(), p2->GetPid());
    // A process's parent is the OS that started it, not 0 (which means no
    // parent at all and is never allocated).
    EXPECT_EQ(p1->GetParentPid(), os->GetOSProcessID());
    EXPECT_EQ(p2->GetParentPid(), os->GetOSProcessID());
    EXPECT_EQ(p1->Path(), "hello.md");
    // An agent-backed process names the agent running it; AsAgent is reachable
    // only from inside the process (ICurrentProcess).
    EXPECT_FALSE(p1->StartingAgentName().empty());
}

TEST_F(HaisosOSTest, EveryOSGetsAPidOfItsOwn) {
    auto os = BuildOS();
    auto other = BuildOS();

    EXPECT_NE(os->GetOSProcessID(), 0u);
    EXPECT_NE(other->GetOSProcessID(), 0u);
    EXPECT_NE(os->GetOSProcessID(), other->GetOSProcessID());
}

TEST_F(HaisosOSTest, StartProcessKeepsTheEnvironmentItWasGiven) {
    auto os = BuildOS();
    auto environment = TestEnvironment();
    environment->SetVariable("GREETING", "hello");

    auto process = os->StartProcess(environment, "hello.md", {}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_NE(process->GetEnvironment(), nullptr);
    EXPECT_EQ(process->GetEnvironment()->GetVariable("GREETING").value_or(""), "hello");
    // The OS's own environment is untouched: nothing is shared implicitly.
    EXPECT_FALSE(os->GetOsEnvironment()->HasVariable("GREETING"));
}

TEST_F(HaisosOSTest, StartProcessWithoutAnEnvironmentReturnsNull) {
    auto os = BuildOS();
    EXPECT_EQ(os->StartProcess(nullptr, "hello.md", {}, /*workingDirectory=*/"", StartProcessOptions{}), nullptr);
}

TEST_F(HaisosOSTest, StartProcessWithUnsupportedExtensionReturnsNull) {
    auto os = BuildOS();
    EXPECT_EQ(os->StartProcess(TestEnvironment(), "not_a_program.txt", {}, /*workingDirectory=*/"", StartProcessOptions{}), nullptr);
}

TEST_F(HaisosOSTest, StartProcessRunsLuaScriptToCompletion) {
    auto os = BuildOS();
    auto process = os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    // Not an agent, so no agent name.
    EXPECT_TRUE(process->StartingAgentName().empty());
    EXPECT_TRUE(process->WaitToFinish(2000));
    // WaitToFinish(0) does not wait; it just reports that it has finished.
    EXPECT_TRUE(process->WaitToFinish(0));
}

TEST_F(HaisosOSTest, StartProcessLuaMissingFileReturnsNull) {
    auto os = BuildOS();
    EXPECT_EQ(os->StartProcess(TestEnvironment(), "missing.lua", {}, /*workingDirectory=*/"", StartProcessOptions{}), nullptr);
}

TEST_F(HaisosOSTest, GetRunningProcessesIncludesStartedProcess) {
    auto os = BuildOS();
    auto process = os->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(process, nullptr);

    auto running = os->GetRunningProcesses();
    bool found = false;
    for (const auto& p : running) {
        if (p->GetPid() == process->GetPid()) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(HaisosOSTest, CreateSubOSIsConfinedByTheRootItIsGiven) {
    // A sub-OS is confined by the filesystem handed to it, not by a permission
    // flag: narrowing the root to "sub" is what puts the parent's files
    // out of reach.
    auto os = BuildOS();
    auto filesystemService = os->GetServicesCreator()->CreateFileSystemService();
    auto subRoot = filesystemService->CreateSubFileSystem(
        m_factory->CreatePhysicalFileSystem(kTestRoot), "sub");
    auto subOS = os->CreateSubOS(
        os->GetServicesCreator()->Clone(),
        m_factory->CreatePhysicalConsole(),
        subRoot,
        nullptr,
        os->GetOsEnvironment()->Clone());
    ASSERT_NE(subOS, nullptr);

    EXPECT_NE(subOS->StartProcess(TestEnvironment(), "inner.md", {}, /*workingDirectory=*/"", StartProcessOptions{}), nullptr);
    // hello.md lives in the parent root, not the sub-root.
    EXPECT_EQ(subOS->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"", StartProcessOptions{}), nullptr);
}

TEST_F(HaisosOSTest, CreateSubOSCarriesTheParentOSPid) {
    auto os = BuildOS();

    auto subOS = os->CreateSubOS(
        os->GetServicesCreator()->Clone(),
        m_factory->CreatePhysicalConsole(),
        m_factory->CreatePhysicalFileSystem(kTestRoot),
        nullptr,
        os->GetOsEnvironment()->Clone());
    ASSERT_NE(subOS, nullptr);
    // A sub-OS is the same OS seen through a narrower root, so it keeps the
    // pid rather than taking one of its own.
    EXPECT_EQ(subOS->GetOSProcessID(), os->GetOSProcessID());
}

TEST_F(HaisosOSTest, SubOSGetsACopyOfTheEnvironmentItIsGiven) {
    auto os = BuildOS();
    EXPECT_EQ(os->GetOsEnvironment()->GetVariable(kEnvModel).value_or(""), "llama3");

    auto subOS = os->CreateSubOS(
        os->GetServicesCreator()->Clone(),
        m_factory->CreatePhysicalConsole(),
        m_factory->CreatePhysicalFileSystem(kTestRoot),
        nullptr,
        os->GetOsEnvironment()->Clone());
    ASSERT_NE(subOS, nullptr);
    EXPECT_EQ(subOS->GetOsEnvironment()->GetVariable(kEnvModel).value_or(""), "llama3");

    // A clone, not a share: what the sub-OS sets stays in the sub-OS.
    subOS->GetOsEnvironment()->SetVariable(kEnvModel, "other-model");
    EXPECT_EQ(os->GetOsEnvironment()->GetVariable(kEnvModel).value_or(""), "llama3");
}

// The current directory belongs to a process, not to a filesystem: a process
// starts where StartProcess put it, moves on its own, and moving does not move
// any other process.
TEST_F(HaisosOSTest, ProcessStartsAtTheWorkingDirectoryItWasGiven) {
    auto os = BuildOS();

    auto atRoot = os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"", StartProcessOptions{});
    auto inSub = os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"sub", StartProcessOptions{});
    ASSERT_NE(atRoot, nullptr);
    ASSERT_NE(inSub, nullptr);

    // Only the process itself sees its working directory, so ask it as itself.
    auto atRootInside = std::dynamic_pointer_cast<ICurrentProcess>(atRoot);
    auto inSubInside = std::dynamic_pointer_cast<ICurrentProcess>(inSub);
    ASSERT_NE(atRootInside, nullptr);
    ASSERT_NE(inSubInside, nullptr);
    EXPECT_EQ(atRootInside->IO()->GetCurrentDirectory(), "/");
    EXPECT_EQ(inSubInside->IO()->GetCurrentDirectory(), "/sub");
}

TEST_F(HaisosOSTest, ChangeDirectoryMovesOnlyThatProcess) {
    auto os = BuildOS();

    auto first = std::dynamic_pointer_cast<ICurrentProcess>(
        os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"", StartProcessOptions{}));
    auto second = std::dynamic_pointer_cast<ICurrentProcess>(
        os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"", StartProcessOptions{}));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    EXPECT_EQ(first->IO()->ChangeDirectory("sub"), 0);
    EXPECT_EQ(first->IO()->GetCurrentDirectory(), "/sub");
    // The other process has not moved: a filesystem holds no cwd for them to share.
    EXPECT_EQ(second->IO()->GetCurrentDirectory(), "/");

    EXPECT_EQ(first->IO()->ChangeDirectory(".."), 0);
    EXPECT_EQ(first->IO()->GetCurrentDirectory(), "/");
}

TEST_F(HaisosOSTest, ChangeDirectoryRejectsWhatIsNotADirectory) {
    auto os = BuildOS();
    auto process = std::dynamic_pointer_cast<ICurrentProcess>(
        os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"", StartProcessOptions{}));
    ASSERT_NE(process, nullptr);

    EXPECT_EQ(process->IO()->ChangeDirectory("hello.md"), -1);
    EXPECT_EQ(process->IO()->ChangeDirectory("nowhere"), -1);
    EXPECT_EQ(process->IO()->GetCurrentDirectory(), "/");
}

// Reading a process's environment from outside must never be a way to change
// what the process itself sees.
TEST_F(HaisosOSTest, GetEnvironmentHandsOutACloneNotTheProcessOwn) {
    auto os = BuildOS();
    auto environment = TestEnvironment();
    environment->SetVariable("GREETING", "hello");
    auto process = os->StartProcess(environment, "hello.md", {}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(process, nullptr);

    auto handedOut = process->GetEnvironment();
    ASSERT_NE(handedOut, nullptr);
    EXPECT_EQ(handedOut->GetVariable("GREETING").value_or(""), "hello");

    handedOut->SetVariable("GREETING", "tampered");
    EXPECT_EQ(process->GetEnvironment()->GetVariable("GREETING").value_or(""), "hello");
}

// A process reaches everything outside itself through its OS, and only through
// ICurrentProcess::OS() -- nothing is handed an IHaisosOS directly.
// That is what will let one process be given a narrower OS than another.
TEST_F(HaisosOSTest, ProcessReachesItsOSThroughOS) {
    auto os = BuildOS();
    auto process = std::dynamic_pointer_cast<ICurrentProcess>(
        os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"", StartProcessOptions{}));
    ASSERT_NE(process, nullptr);

    auto reached = process->OS();
    ASSERT_NE(reached, nullptr);
    EXPECT_EQ(reached->GetOSProcessID(), os->GetOSProcessID());
}

// The reference back to the OS is weak on purpose: an OS owns its processes, so
// a strong one would be a cycle and the OS would never be destroyed.
TEST_F(HaisosOSTest, ProcessDoesNotKeepItsOSAlive) {
    std::shared_ptr<ICurrentProcess> process;
    std::weak_ptr<IHaisosOS> osWatch;
    {
        auto os = BuildOS();
        osWatch = os;
        process = std::dynamic_pointer_cast<ICurrentProcess>(
            os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"", StartProcessOptions{}));
        ASSERT_NE(process, nullptr);
        ASSERT_NE(process->OS(), nullptr);
    }

    EXPECT_TRUE(osWatch.expired());
    EXPECT_EQ(process->OS(), nullptr);
}

// The os_* tools reach the OS only through the calling process
// (ICurrentProcess), which is what lets them resolve a relative path against
// that process's working directory rather than against the OS root.
TEST_F(HaisosOSTest, ToolsResolveRelativePathsAgainstTheCallingProcessDirectory) {
    auto os = BuildOS();

    auto process = os->StartProcess(TestEnvironment(), "write_relative.lua", {}, /*workingDirectory=*/"sub", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(5000));

    EXPECT_TRUE(std::filesystem::exists(kTestRoot + "/sub/written.txt"));
    EXPECT_FALSE(std::filesystem::exists(kTestRoot + "/written.txt"));
}

TEST_F(HaisosOSTest, ToolsResolveAgainstTheRootForAProcessStartedThere) {
    auto os = BuildOS();

    auto process = os->StartProcess(TestEnvironment(), "write_relative.lua", {}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(5000));

    EXPECT_TRUE(std::filesystem::exists(kTestRoot + "/written.txt"));
    EXPECT_FALSE(std::filesystem::exists(kTestRoot + "/sub/written.txt"));
}

// IFileIO is what makes a bare name or a relative path mean anything: an
// IFileSystem understands absolute paths alone, so this is the only place the
// path and the process's position are brought together.
TEST_F(HaisosOSTest, FileIOResolvesBareNamesAndRelativePathsFromWhereTheProcessIs) {
    auto os = BuildOS();
    auto process = std::dynamic_pointer_cast<ICurrentProcess>(
        os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"sub", StartProcessOptions{}));
    ASSERT_NE(process, nullptr);
    auto io = process->IO();
    ASSERT_NE(io, nullptr);

    // A bare name is taken from the working directory.
    EXPECT_EQ(io->ResolvePath("inner.md"), "/sub/inner.md");
    // A relative path is walked from there.
    EXPECT_EQ(io->ResolvePath("../hello.md"), "/hello.md");
    // An absolute path is already what it says.
    EXPECT_EQ(io->ResolvePath("/hello.md"), "/hello.md");
    // ".." cannot climb above the root.
    EXPECT_EQ(io->ResolvePath("../../../hello.md"), "/hello.md");
}

TEST_F(HaisosOSTest, FileIOReadsAndWritesThroughTheWorkingDirectory) {
    auto os = BuildOS();
    auto process = std::dynamic_pointer_cast<ICurrentProcess>(
        os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"sub", StartProcessOptions{}));
    ASSERT_NE(process, nullptr);
    auto io = process->IO();

    auto file = io->OpenFile("note.txt", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(file, nullptr);
    const std::string payload = "written from sub";
    EXPECT_EQ(file->Write(payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));
    file.reset();

    // The bare name landed in the working directory, not at the root.
    EXPECT_TRUE(std::filesystem::exists(kTestRoot + "/sub/note.txt"));
    EXPECT_FALSE(std::filesystem::exists(kTestRoot + "/note.txt"));

    // And reads come back through the same route.
    std::string readBack;
    EXPECT_TRUE(ReadWholeFile(*io, "note.txt", readBack));
    EXPECT_EQ(readBack, payload);

    // A directory listing is resolved the same way.
    bool sawNote = false;
    for (const auto& entry : io->ReadDirectory(".")) {
        if (entry.name == "note.txt") {
            sawNote = true;
        }
    }
    EXPECT_TRUE(sawNote);
}

// OpenFile hands back the open file itself and puts nothing in the process's
// descriptor table; a number is something a caller asks for with AddDescriptor.
TEST_F(HaisosOSTest, OpenFileHandsBackAnUnnumberedDescriptor) {
    auto os = BuildOS();
    auto process = std::dynamic_pointer_cast<ICurrentProcess>(
        os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"", StartProcessOptions{}));
    ASSERT_NE(process, nullptr);
    // The script ends at once, releasing its table as it does: wait for that
    // first, or the release could empty slot 0 between AddDescriptor and
    // GetDescriptor below. The table stays usable after the end.
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    auto io = process->IO();

    auto file = io->OpenFile("x.txt", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(file, nullptr);
    for (int fd = 0; fd <= 3; ++fd) {
        EXPECT_EQ(io->GetDescriptor(fd), nullptr) << "slot " << fd << " held a descriptor after OpenFile";
    }

    EXPECT_EQ(io->AddDescriptor(file), 0);
    file.reset();
    EXPECT_EQ(io->GetDescriptor(0)->Write("hi", 2), 2);
    EXPECT_EQ(io->CloseDescriptor(0), 0);

    EXPECT_EQ(ReadHostFile(kTestRoot + "/x.txt"), "hi");
}

// Every descriptor a process holds is released when its program ends, before
// it reports finished -- here, a Lua script's.
TEST_F(HaisosOSTest, ALuaScriptsDescriptorsAreReleasedWhenItEnds) {
    std::ofstream(kTestRoot + "/spin.lua") << "while true do end";
    auto os = BuildOS();
    auto process = std::dynamic_pointer_cast<ICurrentProcess>(
        os->StartProcess(TestEnvironment(), "spin.lua", {}, /*workingDirectory=*/"", StartProcessOptions{}));
    ASSERT_NE(process, nullptr);

    auto releases = std::make_shared<std::atomic<int>>(0);
    // Slots 0, 1 and 2 already hold the process's standard streams.
    ASSERT_EQ(process->IO()->AddDescriptor(ReleaseCountingDescriptor::Create(releases)), 3);

    // The script is still spinning, so nothing may have been released yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(releases->load(), 0);

    process->TriggerStop();
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    // Already released at the moment WaitToFinish returned -- not after.
    EXPECT_EQ(releases->load(), 1);
}

// And the same for an agent's: its program is its conversation, so the table
// is released when the conversation thread ends (through Agent::SetFinishedHook,
// set by AgentProcess::Create).
TEST_F(HaisosOSTest, AnAgentsDescriptorsAreReleasedWhenItsConversationEnds) {
    auto console = std::make_shared<BlockingPhysicalConsole>();
    // On every path out: a failed assertion must not leave the console blocked,
    // or the input loop waits in ReadLine forever and this test hangs in
    // teardown.
    struct ConsoleReleaser {
        std::shared_ptr<BlockingPhysicalConsole> console;
        ~ConsoleReleaser() { console->Release(); }
    } releaser{console};

    auto os = BuildOS(console);
    StartProcessOptions options;
    options.interactive = true;
    auto process = std::dynamic_pointer_cast<ICurrentProcess>(
        os->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"", options));
    ASSERT_NE(process, nullptr);

    auto releases = std::make_shared<std::atomic<int>>(0);
    // Slots 0, 1 and 2 already hold the process's standard streams.
    ASSERT_EQ(process->IO()->AddDescriptor(ReleaseCountingDescriptor::Create(releases)), 3);

    // The input loop is blocked on the console, so the conversation is open.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(releases->load(), 0);

    console->Release();
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    EXPECT_EQ(releases->load(), 1);
}

TEST_F(HaisosOSTest, CreateHaisosOSWithoutAnEnvironmentReturnsNull) {
    std::shared_ptr<IServicesCreator> servicesCreator = m_factory->CreateServicesCreator();
    std::shared_ptr<IFileSystem> rootFileSystem = m_factory->CreatePhysicalFileSystem(kTestRoot);
    auto physicalConsole = m_factory->CreatePhysicalConsole();
    EXPECT_EQ(
        m_factory->CreateHaisosOS(std::move(servicesCreator), physicalConsole, rootFileSystem, nullptr, nullptr),
        nullptr);
}

} // namespace

TEST_F(HaisosOSTest, AnAgentProcessIsNotInteractiveByDefault) {
    auto console = std::make_shared<ScriptedPhysicalConsole>(std::vector<std::string>{"never read"});
    auto os = BuildOS(console);

    auto process = os->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    EXPECT_EQ(console->ReadLineCalls(), 0);
    auto agent = std::dynamic_pointer_cast<ICurrentProcess>(process)->AsAgent();
    EXPECT_FALSE(agent->IsInteractive());
}

TEST_F(HaisosOSTest, AnInteractiveAgentIsFedConsoleLinesUntilInputEnds) {
    auto console = std::make_shared<ScriptedPhysicalConsole>(std::vector<std::string>{"typed by the user"});
    auto os = BuildOS(console);
    StartProcessOptions options;
    options.interactive = true;

    auto process = os->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"", options);
    ASSERT_NE(process, nullptr);
    // End of input stops the agent, so the process finishes without a
    // self_close.
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    // The line, then end of input.
    EXPECT_EQ(console->ReadLineCalls(), 2);

    auto agent = std::dynamic_pointer_cast<ICurrentProcess>(process)->AsAgent();
    ASSERT_NE(agent, nullptr);
    EXPECT_TRUE(agent->IsInteractive());
    auto history = agent->GetHistory();
    EXPECT_TRUE(HistoryMentions(history, "Say hello."));
    EXPECT_TRUE(HistoryMentions(history, "typed by the user"));
}

// `-i` is not agents-only: a script run with it gets the console's input as
// its stdin (its Lua runtime does not read it yet -- that is a later task),
// and the script still runs and finishes like any other.
TEST_F(HaisosOSTest, AnInteractiveScriptIsNotAnAgent) {
    auto console = std::make_shared<ScriptedPhysicalConsole>(std::vector<std::string>{"never read"});
    auto os = BuildOS(console);
    StartProcessOptions options;
    options.interactive = true;

    auto process = os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"", options);
    ASSERT_NE(process, nullptr);
    EXPECT_TRUE(process->WaitToFinish(kProcessWaitMs));
    EXPECT_EQ(console->ReadLineCalls(), 0);
}

// A given stdin wins over the default: the console's input is never read, and
// the interactive agent is fed the descriptor's lines instead.
TEST_F(HaisosOSTest, AnInteractiveAgentReadsAGivenStdin) {
    auto console = std::make_shared<ScriptedPhysicalConsole>(std::vector<std::string>{"never read"});
    auto os = BuildOS(console);

    auto input = std::make_shared<Mocks::MockFileDescriptor>();
    input->Feed("from a pipe\n");
    input->EndInput();
    StartProcessOptions options;
    options.interactive = true;
    options.stdIn = input;

    auto process = os->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"", options);
    ASSERT_NE(process, nullptr);
    // End of the pipe's input stops the agent, as end of console input would.
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    EXPECT_EQ(console->ReadLineCalls(), 0);

    auto agent = std::dynamic_pointer_cast<ICurrentProcess>(process)->AsAgent();
    ASSERT_NE(agent, nullptr);
    auto history = agent->GetHistory();
    EXPECT_TRUE(HistoryMentions(history, "Say hello."));
    EXPECT_TRUE(HistoryMentions(history, "from a pipe"));
}

// An LLM round trip that fails is a diagnostic, not an answer: it belongs on
// the process's stderr, with nothing on its stdout.
TEST_F(HaisosOSTest, AnAgentsLLMFailureGoesToStderr) {
    auto os = BuildOS();

    auto out = std::make_shared<Mocks::MockFileDescriptor>();
    auto err = std::make_shared<Mocks::MockFileDescriptor>();
    StartProcessOptions options;
    options.stdOut = out;
    options.stdErr = err;
    auto process = os->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"", options);
    ASSERT_NE(process, nullptr);
    // Wait for the agent before reading the mocks back: it writes them on its
    // own runtime thread.
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));

    // The endpoint is unreachable, so the one round trip failed.
    EXPECT_NE(err->Written().find("Error: HTTP request failed"), std::string::npos);
    EXPECT_EQ(out->Written().find("Error:"), std::string::npos);
}

// --- Exit codes ---

// An agent whose (only) command failed at the LLM round trip exits 1, as a
// shell reports its last command's outcome.
TEST_F(HaisosOSTest, AnAgentWhoseLLMCallFailsExitsOne) {
    auto os = BuildOS();
    auto process = os->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(*process->ExitCode(), 1);
}

// A stop asked for once the process has finished is not a stop at all: the
// code it already earned is kept.
TEST_F(HaisosOSTest, AStopAfterFinishingKeepsTheExitCode) {
    auto os = BuildOS();
    auto process = os->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    ASSERT_EQ(process->ExitCode().value_or(-1), 1);

    process->TriggerStop();
    EXPECT_EQ(process->ExitCode().value_or(-1), 1);
}

TEST_F(HaisosOSTest, AScriptExitCodeIsVisibleFromOutside) {
    std::ofstream(kTestRoot + "/exit5.lua") << "exit(5)";
    auto os = BuildOS();

    auto process = os->StartProcess(TestEnvironment(), "exit5.lua", {}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(*process->ExitCode(), 5);
}

TEST_F(HaisosOSTest, OsListProcessesShowsExitCodes) {
    std::ofstream(kTestRoot + "/loop.lua") << "while true do end";
    std::ofstream(kTestRoot + "/exit5.lua") << "exit(5)";
    auto os = BuildOS();

    auto looping = os->StartProcess(TestEnvironment(), "loop.lua", {}, /*workingDirectory=*/"", StartProcessOptions{});
    auto exited = os->StartProcess(TestEnvironment(), "exit5.lua", {}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(looping, nullptr);
    ASSERT_NE(exited, nullptr);
    ASSERT_TRUE(exited->WaitToFinish(kProcessWaitMs));

    // Build the tool as the finished process would have it (its OS() is still
    // this OS), and call it before any further StartProcess, which would prune
    // the finished process from the list.
    auto handle = CurrentProcessHandle::Create();
    handle->Set(std::dynamic_pointer_cast<ICurrentProcess>(exited));
    auto tool = OSToolFactory::Create(handle)->CreateTool("os_list_processes");
    ASSERT_NE(tool, nullptr);
    const ToolResult result = tool->Call(nullptr, nlohmann::json::object());
    ASSERT_FALSE(result.isError) << result.content;
    const auto listed = nlohmann::json::parse(result.content);
    ASSERT_TRUE(listed.is_array()) << result.content;

    bool sawLooping = false;
    bool sawExited = false;
    for (const auto& entry : listed) {
        if (entry.value("path", "") == "loop.lua") {
            sawLooping = true;
            // Still running: no code yet.
            EXPECT_TRUE(entry["exit_code"].is_null()) << entry.dump();
        } else if (entry.value("path", "") == "exit5.lua") {
            sawExited = true;
            EXPECT_EQ(entry["exit_code"], 5) << entry.dump();
        }
    }
    EXPECT_TRUE(sawLooping) << result.content;
    EXPECT_TRUE(sawExited) << result.content;

    looping->TriggerStop();
    ASSERT_TRUE(looping->WaitToFinish(kProcessWaitMs));
    EXPECT_EQ(looping->ExitCode().value_or(-1), 143);
}

// A script's print goes to the process's stdout -- a given one, not the
// console behind the default.
TEST_F(HaisosOSTest, AScriptPrintsToAGivenStdout) {
    std::ofstream(kTestRoot + "/greet.lua") << "print('hi')";
    auto console = std::make_shared<ScriptedPhysicalConsole>(std::vector<std::string>{});
    auto os = BuildOS(console);

    auto out = std::make_shared<Mocks::MockFileDescriptor>();
    StartProcessOptions options;
    options.stdOut = out;
    auto process = os->StartProcess(TestEnvironment(), "greet.lua", {}, /*workingDirectory=*/"", options);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));

    EXPECT_EQ(out->Written(), "hi\n");
    EXPECT_EQ(console->Written(), "");
}

// --- A script and the real OS tools ---

// A file of JSON nested deeper than the Lua bridge converts reaches the script
// reading it as its text. Converting it into tables used to recurse until the
// Lua stack overflowed, taking the whole program down -- and any script that
// reads a file someone else wrote could be made to.
TEST_F(HaisosOSTest, AScriptReadingDeeplyNestedJsonGetsItsText) {
    std::ofstream(kTestRoot + "/nested.json") << std::string(300, '[') << std::string(300, ']');
    std::ofstream(kTestRoot + "/read_nested.lua")
        << "local r, is_error = os_read_file({path = 'nested.json'})\n"
        << "os_write_file({path = 'nested_result.txt', content = type(r) .. '|' .. tostring(is_error) .. '|' .. #r})\n";
    auto os = BuildOS();

    auto process = os->StartProcess(TestEnvironment(), "read_nested.lua", {}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    EXPECT_EQ(ReadHostFile(kTestRoot + "/nested_result.txt"), "string|false|600");
}

// A wrongly typed argument is refused with a message naming it -- never read
// as the default, and never thrown out of the tool, which used to end an agent
// calling it. "append" as the string "true" read as false would have
// overwritten the file instead of appending to it.
TEST_F(HaisosOSTest, OSToolsRefuseWronglyTypedArguments) {
    std::ofstream(kTestRoot + "/keep.txt") << "original";
    std::ofstream(kTestRoot + "/typed_args.lua")
        << "local lines = {}\n"
        << "local function try(name, tool, args)\n"
        << "  local result, is_error = tool(args)\n"
        << "  lines[#lines + 1] = name .. '=' .. tostring(is_error) .. ':' .. result\n"
        << "end\n"
        << "try('write', os_write_file, {path = 'keep.txt', content = 'new', append = 'true'})\n"
        << "try('list', os_list_directory, {path = 5})\n"
        << "try('read', os_read_file, {path = true})\n"
        << "try('start', os_start_process, {path = 'script.lua', args = {1, 2}})\n"
        << "os_write_file({path = 'typed_result.txt', content = table.concat(lines, '\\n')})\n";
    auto os = BuildOS();

    auto process = os->StartProcess(TestEnvironment(), "typed_args.lua", {}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    EXPECT_EQ(ReadHostFile(kTestRoot + "/keep.txt"), "original");
    EXPECT_EQ(ReadHostFile(kTestRoot + "/typed_result.txt"),
        "write=true:Invalid field append: expected a boolean (true or false), got a string\n"
        "list=true:Invalid field path: expected a string, got a number\n"
        "read=true:Invalid field path: expected a string, got a boolean\n"
        "start=true:Invalid field args: expected an array of strings, got an array holding a number");
}

// --- Builtin commands ---

TEST_F(HaisosOSTest, StartProcessRunsABuiltinTheRootFilesystemNames) {
    auto os = BuildOS();
    auto root = os->GetRootFileSystem();
    ASSERT_TRUE(m_factory->CreateBuiltinConfigurator()->AddBuiltinCommand(root, "/sub/mk", "mkdir"));

    // Relative program paths are resolved against the root, as for any program.
    auto process = os->StartProcess(TestEnvironment(), "sub/mk", {"/made"}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    EXPECT_EQ(process->Path(), "sub/mk");
    EXPECT_EQ(process->GetParentPid(), os->GetOSProcessID());
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    EXPECT_TRUE(std::filesystem::is_directory(kTestRoot + "/made"));
    // The builtin exists only on the filesystem, never on disk.
    EXPECT_FALSE(std::filesystem::exists(kTestRoot + "/sub/mk"));
}

TEST_F(HaisosOSTest, AnOSWithoutBuiltinCommandsCannotStartABuiltin) {
    auto root = m_factory->CreatePhysicalFileSystem(kTestRoot);
    ASSERT_TRUE(m_factory->CreateBuiltinConfigurator()->AddBuiltinCommand(root, "/echo", "echo"));
    auto os = m_factory->CreateHaisosOS(
        m_factory->CreateServicesCreator(), m_factory->CreatePhysicalConsole(), root, /*builtinCommands=*/nullptr, TestEnvironment());
    ASSERT_NE(os, nullptr);
    EXPECT_EQ(os->StartProcess(TestEnvironment(), "/echo", {"hi"}, "", StartProcessOptions{}), nullptr);
}

// Default options mean the console: a builtin's stdout and stderr go to the
// host's stdout and stderr through the console descriptors.
TEST_F(HaisosOSTest, ABuiltinWithoutGivenStreamsWritesToTheConsole) {
    auto console = std::make_shared<ScriptedPhysicalConsole>(std::vector<std::string>{});
    auto os = BuildOS(console);
    auto root = os->GetRootFileSystem();
    ASSERT_EQ(root->CreateDirectory("/bin", 0755), 0);
    ASSERT_TRUE(m_factory->CreateBuiltinConfigurator()->AddBuiltinCommand(root, "/bin/echo", "echo"));

    auto process = os->StartProcess(TestEnvironment(), "/bin/echo", {"hi"}, /*workingDirectory=*/"", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    EXPECT_EQ(console->Written(), "hi\n");
}

// A given stream wins over the default: stdout bypasses the console entirely.
TEST_F(HaisosOSTest, AGivenStdoutBypassesTheConsole) {
    auto console = std::make_shared<ScriptedPhysicalConsole>(std::vector<std::string>{});
    auto os = BuildOS(console);
    auto root = os->GetRootFileSystem();
    ASSERT_EQ(root->CreateDirectory("/bin", 0755), 0);
    ASSERT_TRUE(m_factory->CreateBuiltinConfigurator()->AddBuiltinCommand(root, "/bin/echo", "echo"));

    auto out = std::make_shared<Mocks::MockFileDescriptor>();
    StartProcessOptions options;
    options.stdOut = out;
    auto process = os->StartProcess(TestEnvironment(), "/bin/echo", {"hi"}, /*workingDirectory=*/"", options);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kProcessWaitMs));
    EXPECT_EQ(out->Written(), "hi\n");
    EXPECT_EQ(console->Written(), "");
}

// --- Objects released last on their own threads ---
//
// THE PROBLEM
//
// A process's thread can hold the last reference to its own OS, and to its own
// process object. An os_* tool fetches both from the calling process for the
// length of its call (OSToolContext holds `process`, `io` and `os`, all
// strong); an agent hands shared_from_this() to every tool it calls; a
// builtin's ProcessFileIO holds the OS for a moment on every file operation.
// When everyone else lets go in the meantime, those references are the last,
// and they go when the call returns -- on the process's own thread. That is
// what haisos's main() does as it returns: it waits for the RUN processes only,
// so a child started with os_start_process may still be inside a tool call.
// Before the fix, the destructors then ran on that thread:
//
//   1. ~HaisosOS. It asks each process to stop and waits up to 5 s for it, one
//      after the other -- including the process whose thread it is running on,
//      which cannot finish while it waits. 5 s were lost for nothing, and the
//      log said only "did not stop within 5000ms during destruction".
//   2. ~LuaProcess, when the tool call let go of `process` -- the last
//      reference once the OS had dropped its own. It waited another 5 s for
//      itself, then joined its own thread. join() throws std::system_error
//      (EDEADLK), which leaving a destructor is std::terminate: the program
//      died of SIGABRT, printing "terminate called without an active
//      exception" (GCC's landing pad for the noexcept destructor calls
//      std::terminate directly, so the exception never shows). ~BuiltinProcess
//      joins at once, so a builtin died the same way, only sooner.
//   3. ~Agent, when the reference an agent hands its own tool call was the
//      last. It waits for its thread in a loop that never gives up, so it hung
//      forever, logging "has not stopped after waiting 5000ms in its
//      destructor, still waiting" every 5 s (see
//      AgentTest.AnAgentReleasedLastByItsOwnToolCallIsDestroyedOffItsThread).
//
// The log could not show any of it: its lines did not say which thread wrote
// them.
//
// THE FIX
//
// Nothing whose destructor waits for a runtime thread is destroyed on one (see
// src/components/libheaders/DestroyOffRuntimeThreads.h). HaisosOS, the process
// classes and Agent are created with the DestroyOffRuntimeThreads deleter, and
// every runtime thread runs inside a RuntimeThreadScope. Released on a runtime
// thread, such an object is handed to the one DestructionThread, while the
// runtime thread carries on: the object stays whole until its destructor --
// running elsewhere now -- has waited that thread out.
//
// WHAT TO LOOK FOR IN THE LOG
//
//   * Every line names its thread: "main", "agent <name>", "lua <path>
//     pid=<n>", "builtin <path> pid=<n>", "input <agent>", "destruction", or
//     "t<n>" for a thread nobody named.
//   * "<what>: its last reference was released on a runtime thread, so it is
//     destroyed on the destruction thread" (INFO) marks a hand-off; the
//     destruction thread's "destroying <what>" and "destroyed <what> in <n>ms"
//     (DEBUG) time it.
//   * Each destructor logs "<what>: destroying" (DEBUG), on the thread it runs
//     on.
//   * An ERROR "waiting <n>ms for itself on its own thread", "destroyed on its
//     own thread" or "being destroyed on runtime thread" means something slipped
//     past the rule: the problem is back.
//
// HOW THESE TESTS REPRODUCE IT
//
// read_held.lua reads /held.txt with os_read_file, and the root is a
// GatedFileSystem, which holds that open until the test releases it: the
// script is then inside the tool call, holding its OS and its process. The
// test lets go of its own references, checks that only the call keeps the OS
// alive, and releases the gate. Before the fix, the first test failed after
// 5 s, and the second one's child process died of SIGABRT after 10 s.

// Symptom 1: the OS's last reference goes with the tool call, on the script's
// thread. The script has nothing left to do once the read returns, so it must
// finish at once -- not after ~HaisosOS, run on its thread, has spent 5 s
// waiting for it to stop.
TEST_F(HaisosOSTest, AnOSReleasedLastByAToolCallDoesNotWaitForTheCallingProcess) {
    auto gate = std::make_shared<GatedFileSystem>(m_factory->CreatePhysicalFileSystem(kTestRoot), kHeldPath);
    std::weak_ptr<IHaisosOS> osWatch;
    std::shared_ptr<IProcess> process;
    {
        auto os = BuildOS(nullptr, gate);
        osWatch = os;
        process = os->StartProcess(TestEnvironment(), "read_held.lua", {}, /*workingDirectory=*/"", StartProcessOptions{});
        ASSERT_NE(process, nullptr);
        ASSERT_TRUE(gate->WaitUntilHeld(kProcessWaitMs));
    }
    // Only the held tool call keeps the OS alive now.
    EXPECT_FALSE(osWatch.expired());

    gate->Release();
    // The script has nothing left to do once the read returns.
    EXPECT_TRUE(process->WaitToFinish(2000))
        << "the script's thread was held up by ~HaisosOS waiting for the script to stop";
    // Not an ASSERT: should the problem come back, the process must still
    // finish before this test lets go of it, or the tool call's reference to
    // it would be the last, and the whole test program would go down with
    // symptom 2.
    EXPECT_TRUE(process->WaitToFinish(kProcessWaitMs));
    EXPECT_TRUE(osWatch.expired());
}

#if defined(GTEST_HAS_DEATH_TEST) && !defined(__EMSCRIPTEN__)
// Symptom 2: let go of the process too, and the tool call's reference to it is
// the last. The process must be destroyed off the script's thread, once that
// thread is done with it -- not on it, where it would join itself and end in
// std::terminate. Run in a child process, so that should the problem come
// back, only the child goes down.
TEST_F(HaisosOSTest, AProcessReleasedLastByItsOwnToolCallDoesNotAbortTheProgram) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    auto letGoDuringAToolCall = [this]() {
        auto console = std::make_shared<ScriptedPhysicalConsole>(std::vector<std::string>{});
        // Both the OS and the process hold the console, so it is let go of
        // only once both have been torn down.
        std::weak_ptr<IPhysicalConsole> consoleWatch = console;
        auto gate = std::make_shared<GatedFileSystem>(m_factory->CreatePhysicalFileSystem(kTestRoot), kHeldPath);
        {
            auto os = BuildOS(console, gate);
            auto process = os->StartProcess(TestEnvironment(), "read_held.lua", {}, /*workingDirectory=*/"", StartProcessOptions{});
            if (!process || !gate->WaitUntilHeld(kProcessWaitMs)) {
                std::fprintf(stderr, "read_held.lua never reached the gate\n");
                std::_Exit(2);
            }
        }
        console.reset();
        gate->Release();

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kProcessWaitMs);
        while (!consoleWatch.expired() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!consoleWatch.expired()) {
            std::fprintf(stderr, "the OS and the process were not torn down within %llums\n",
                static_cast<unsigned long long>(kProcessWaitMs));
            std::_Exit(1);
        }
        // _Exit, not exit: nothing here should depend on static destruction.
        std::_Exit(0);
    };
    EXPECT_EXIT(letGoDuringAToolCall(), ::testing::ExitedWithCode(0), "");
}
#endif
