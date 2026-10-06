#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <vector>
#include "Factory.h"
#include "BuiltinCommand.h"
#include "BuiltinProcess.h"
#include "BuiltinCommandList.h"
#include "ProcessFileIO.h"
#include "src/components/Filesystem/BuiltinCommandFile.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "tests/mocks/MockFileDescriptor.h"

#ifndef _WIN32
#include <utime.h>
#endif

using namespace Haisos;

namespace {

constexpr uint64_t kWaitMs = 10000;

// A physical console that keeps what is written to it: the raw bytes of each
// stream separately (m_out, m_err) and of both in the order written (m_all).
class CapturingConsole : public IPhysicalConsole {
public:
    void Write(const std::string& bytes) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_out += bytes;
        m_all += bytes;
    }
    void WriteError(const std::string& bytes) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_err += bytes;
        m_all += bytes;
    }
    std::optional<std::string> ReadLine() override { return std::nullopt; }
    void Start() override {}
    void Stop() override {}

    // Everything written so far, split on '\n': no empty last element for a
    // trailing newline, a trailing partial line kept. Clears all three strings.
    std::vector<std::string> TakeLines() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> lines;
        std::string line;
        for (char c : m_all) {
            if (c == '\n') {
                lines.push_back(line);
                line.clear();
            } else {
                line += c;
            }
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
        m_out.clear();
        m_err.clear();
        m_all.clear();
        return lines;
    }

    // The raw string its own stream got, taken (only that stream is cleared, so
    // TakeOut() and TakeErr() may be asked in either order).
    std::string TakeOut() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string result;
        result.swap(m_out);
        return result;
    }
    std::string TakeErr() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string result;
        result.swap(m_err);
        return result;
    }

private:
    std::mutex m_mutex;
    std::string m_out;
    std::string m_err;
    std::string m_all;
};

#ifdef _WIN32
constexpr int kDirMode = _S_IREAD | _S_IWRITE;
#else
constexpr int kDirMode = S_IRWXU;
#endif

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

// A command that runs until asked to stop, so a test can hold a builtin
// process alive mid-run and watch what happens to its descriptors at the end.
class WaitCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "wait"; }
    std::string Version() const override { return "1"; }
    const std::vector<BuiltinOption>& Options() const override { return m_options; }
    BuiltinHelp Help() const override { return BuiltinHelp{"wait for a stop request", {"wait"}, ""}; }
    int Run(BuiltinContext& context) override {
        while (!context.StopRequested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return 0;
    }

private:
    std::vector<BuiltinOption> m_options;
};

// A process that exists only to hold a descriptor table, so a BuiltinContext
// can be driven directly, without starting anything.
class FakeProcess : public ICurrentProcess {
public:
    explicit FakeProcess(std::shared_ptr<ProcessFileIO> io) : m_io(std::move(io)) {}

    uint64_t GetPid() const override { return 1; }
    uint64_t GetParentPid() const override { return 0; }
    std::string Path() const override { return "/fake"; }
    std::string StartingAgentName() const override { return std::string(); }
    std::shared_ptr<IEnvironment> GetEnvironment() const override { return nullptr; }
    void TriggerStop() override {}
    bool WaitToFinish(uint64_t) override { return true; }
    std::optional<int> ExitCode() const override { return 0; }

    std::shared_ptr<IFileIO> IO() const override { return m_io; }
    std::shared_ptr<IAgent> AsAgent() override { return nullptr; }
    std::shared_ptr<IHaisosOS> OS() const override { return nullptr; }

private:
    std::shared_ptr<ProcessFileIO> m_io;
};

// The command object of that name from the standard set, or null.
std::shared_ptr<IBuiltinCommand> FindStandardCommand(const std::string& name) {
    for (auto& command : CreateStandardBuiltinCommands()) {
        if (command->Name() == name) {
            return command;
        }
    }
    return nullptr;
}

class BuiltinCommandsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = factory->CreateServicesCreator()->CreateFileSystemService()->CreateEmptyInMemFileSystem();
        ASSERT_EQ(root->CreateDirectory("/bin", kDirMode), 0);
        auto configurator = factory->CreateBuiltinConfigurator();
        for (const auto& name : builtins->GetCommands()) {
            std::string error;
            ASSERT_TRUE(configurator->AddBuiltinCommand(root, "/bin/" + name, name, &error)) << error;
        }
        WriteFile("/notes.txt", "one\ntwo\n\n\n\tthree\n");
        WriteFile("/.hidden", "h");
        ASSERT_EQ(root->CreateDirectory("/docs", kDirMode), 0);
        WriteFile("/docs/a.md", "alpha");
        ASSERT_EQ(root->CreateDirectory("/docs/sub", kDirMode), 0);
        WriteFile("/docs/sub/b.md", "bravo!");

        auto environment = factory->CreateEnvironment();
        os = factory->CreateHaisosOS(factory->CreateServicesCreator(), console, root, builtins, environment);
        ASSERT_NE(os, nullptr);
    }

    void WriteFile(const std::string& path, const std::string& content) {
        auto file = root->OpenFile(path, kFileOpenWriteCreateTruncate, kFileCreateMode);
        ASSERT_NE(file, nullptr) << path;
        file->Write(content.data(), content.size());
    }

    // Runs /bin/<command> with args from workingDirectory, waits for it, and
    // returns what it printed; its exit code goes to *status.
    std::vector<std::string> Run(const std::string& command, const std::vector<std::string>& args,
                                 int* status = nullptr, const std::string& workingDirectory = "/") {
        auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/" + command, args, workingDirectory, StartProcessOptions{});
        EXPECT_NE(process, nullptr) << command;
        if (!process) {
            return {};
        }
        EXPECT_TRUE(process->WaitToFinish(kWaitMs)) << command;
        if (status) {
            // The exit code is set before the process reports finished, so
            // after the wait it is always there.
            EXPECT_TRUE(process->ExitCode().has_value()) << command;
            *status = process->ExitCode().value_or(-1);
        }
        return console->TakeLines();
    }

    static bool Contains(const std::vector<std::string>& lines, const std::string& text) {
        return std::any_of(lines.begin(), lines.end(),
            [&text](const std::string& line) { return line.find(text) != std::string::npos; });
    }

    std::shared_ptr<IFactory> factory = CreateFactory();
    std::shared_ptr<IBuiltinCommands> builtins = factory->CreateBuiltinCommands();
    std::shared_ptr<CapturingConsole> console = std::make_shared<CapturingConsole>();
    std::shared_ptr<IFileSystem> root;
    std::shared_ptr<IHaisosOS> os;
};

using Lines = std::vector<std::string>;

// ls -l lines with their time column ("Sep 25 13:22" or "Sep 25  2025")
// replaced by "<time>", for comparing what does not depend on the clock.
Lines WithoutTimes(Lines lines) {
    static const std::regex kTime("[A-Z][a-z]{2} [ 123][0-9] ([0-9]{2}:[0-9]{2}| [0-9]{4}) ");
    for (auto& line : lines) {
        line = std::regex_replace(line, kTime, "<time> ", std::regex_constants::format_first_only);
    }
    return lines;
}

} // namespace

// --- IBuiltinCommands ---

TEST_F(BuiltinCommandsTest, ListsEveryBuiltinSortedWithAVersion) {
    const auto commands = builtins->GetCommands();
    EXPECT_EQ(commands, (Lines{"cat", "echo", "ls", "mkdir", "pwd"}));
    for (const auto& name : commands) {
        EXPECT_FALSE(builtins->GetBuiltinVersion(name).empty()) << name;
    }
    EXPECT_TRUE(builtins->GetBuiltinVersion("nosuch").empty());
}

TEST_F(BuiltinCommandsTest, RunCommandRefusesWhatItCannotRun) {
    BuiltinCommandHost host;
    host.os = os;
    auto environment = factory->CreateEnvironment();
    EXPECT_EQ(builtins->RunCommand(host, environment, "nosuch", {}, "/", StartProcessOptions{}), nullptr);
    EXPECT_EQ(builtins->RunCommand(host, nullptr, "echo", {}, "/", StartProcessOptions{}), nullptr);
    EXPECT_EQ(builtins->RunCommand(BuiltinCommandHost{}, environment, "echo", {}, "/", StartProcessOptions{}), nullptr);
    // A valid host and environment, but no standard streams given: a builtin is
    // only ever run with its descriptors 0/1/2 filled (the OS resolves them).
    EXPECT_EQ(builtins->RunCommand(host, environment, "echo", {}, "/", StartProcessOptions{}), nullptr);
}

TEST_F(BuiltinCommandsTest, ABuiltinProcessLooksLikeAnyOther) {
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/pwd", {}, "/", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    EXPECT_EQ(process->Path(), "/bin/pwd");
    EXPECT_EQ(process->GetParentPid(), os->GetOSProcessID());
    EXPECT_TRUE(process->StartingAgentName().empty());
    EXPECT_TRUE(process->WaitToFinish(kWaitMs));
}

TEST_F(BuiltinCommandsTest, EveryBuiltinsHelpHasTheSameShape) {
    for (const auto& command : CreateStandardBuiltinCommands()) {
        const std::string name = command->Name();
        int status = -1;
        auto help = Run(name, {"--help"}, &status);
        EXPECT_EQ(status, 0) << name;
        ASSERT_GE(help.size(), 5u) << name;
        EXPECT_EQ(help[0].rfind("HaisosOS " + name + " version " + command->Version() + " - ", 0), 0u) << help[0];
        EXPECT_EQ(help[1], "Based on Linux " + name + ": https://man7.org/linux/man-pages/man1/" + name + ".1.html");
        EXPECT_EQ(help[2], "");
        EXPECT_EQ(help[3].rfind("Usage: " + name, 0), 0u) << help[3];
        EXPECT_TRUE(Contains(help, "      --help"));
        EXPECT_TRUE(Contains(help, "      --version"));

        // The last line lists every option Haisos does not treat, and only those.
        const std::string& last = help.back();
        ASSERT_EQ(last.rfind("Not treated arguments: ", 0), 0u) << name << ": " << last;
        bool anyNotTreated = false;
        for (const auto& option : command->Options()) {
            const std::string spelling = option.longName.empty()
                ? std::string("-") + option.shortName : "--" + option.longName;
            if (option.id == kBuiltinNotTreated) {
                anyNotTreated = true;
                EXPECT_NE(last.find(spelling), std::string::npos) << name << ": " << spelling;
            } else {
                EXPECT_FALSE(option.description.empty()) << name << ": " << spelling;
                EXPECT_TRUE(Contains(Lines(help.begin(), help.end() - 1), spelling)) << name << ": " << spelling;
            }
        }
        if (!anyNotTreated) {
            EXPECT_EQ(last, "Not treated arguments: none");
        }
    }
}

TEST_F(BuiltinCommandsTest, EveryBuiltinHasAVersion) {
    for (const auto& name : builtins->GetCommands()) {
        int status = -1;
        auto version = Run(name, {"--version"}, &status);
        EXPECT_EQ(status, 0) << name;
        EXPECT_EQ(version, (Lines{name + " (HaisosOS builtin) " + builtins->GetBuiltinVersion(name)})) << name;
    }
}

TEST_F(BuiltinCommandsTest, EveryUntreatedOptionIsAcceptedAndReported) {
    for (const auto& command : CreateStandardBuiltinCommands()) {
        const std::string name = command->Name();
        for (const auto& option : command->Options()) {
            if (option.id != kBuiltinNotTreated) {
                continue;
            }
            const std::string spelling = option.longName.empty()
                ? std::string("-") + option.shortName : "--" + option.longName;
            std::vector<std::string> args = {spelling};
            if (option.argument == BuiltinArgument::Required) {
                args.push_back("x");
            }
            args.push_back("/docs");
            auto lines = Run(name, args);
            EXPECT_TRUE(Contains(lines, "Parameter " + spelling + " is not treated by HaisosOS " + name + " v. " + command->Version()))
                << name << " " << spelling;
            EXPECT_FALSE(Contains(lines, "invalid option")) << name << " " << spelling;
            EXPECT_FALSE(Contains(lines, "unrecognized option")) << name << " " << spelling;
        }
    }
}

TEST_F(BuiltinCommandsTest, AnOptionNoRealCommandHasIsStillAnError) {
    int status = 0;
    auto lines = Run("ls", {"--no-such-option"}, &status);
    EXPECT_EQ(status, 2);
    EXPECT_TRUE(Contains(lines, "ls: unrecognized option '--no-such-option'"));
    lines = Run("mkdir", {"-y", "/x"}, &status);
    EXPECT_EQ(status, 1);
    EXPECT_TRUE(Contains(lines, "mkdir: invalid option -- 'y'"));
}

// --- IBuiltinConfigurator ---

TEST_F(BuiltinCommandsTest, ConfiguratorEnforcesItsRules) {
    auto configurator = factory->CreateBuiltinConfigurator();
    std::string error;
    EXPECT_FALSE(configurator->AddBuiltinCommand(root, "/nodir/echo", "echo", &error));
    EXPECT_NE(error.find("does not exist"), std::string::npos) << error;
    EXPECT_FALSE(configurator->AddBuiltinCommand(root, "/notes.txt/echo", "echo", &error));
    EXPECT_NE(error.find("not a directory"), std::string::npos) << error;
    EXPECT_FALSE(configurator->AddBuiltinCommand(root, "/notes.txt", "echo", &error));
    EXPECT_NE(error.find("already exists"), std::string::npos) << error;
    EXPECT_FALSE(configurator->AddBuiltinCommand(root, "/bin/echo", "cat", &error));
    EXPECT_NE(error.find("already there"), std::string::npos) << error;
    EXPECT_FALSE(configurator->AddBuiltinCommand(root, "bin/other", "echo", &error));
    EXPECT_FALSE(configurator->AddBuiltinCommand(root, "/", "echo", &error));
    EXPECT_FALSE(configurator->AddBuiltinCommand(nullptr, "/bin/x", "echo", &error));

    EXPECT_TRUE(configurator->AddBuiltinCommand(root, "/docs/say", "echo", &error)) << error;
    EXPECT_TRUE(configurator->RemoveBuiltin(root, "/docs/say"));
    EXPECT_FALSE(configurator->RemoveBuiltin(root, "/docs/say"));
}

TEST_F(BuiltinCommandsTest, ABuiltinRunsFromWhereverItIsPlacedWhateverItsName) {
    auto configurator = factory->CreateBuiltinConfigurator();
    ASSERT_TRUE(configurator->AddBuiltinCommand(root, "/docs/say.md", "echo"));
    // A .md path, yet the builtin runs rather than an agent.
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/docs/say.md", {"hi"}, "/", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    EXPECT_EQ(console->TakeLines(), (Lines{"hi"}));
}

TEST_F(BuiltinCommandsTest, AProcessCanReadButNotWriteABuiltin) {
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/pwd", {}, "/", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    auto builtin = std::dynamic_pointer_cast<ICurrentProcess>(process);
    ASSERT_NE(builtin, nullptr);
    auto io = builtin->IO();
    EXPECT_EQ(io->IsBuiltinCommand("/bin/ls").value_or(""), "ls");
    EXPECT_FALSE(io->IsBuiltinCommand("/notes.txt").has_value());
    // Stat resolves against the working directory, like every IFileIO call.
    FileStatus status;
    ASSERT_EQ(io->Stat("bin/ls", status), 0);
    EXPECT_EQ(status.size, BuiltinCommandFileContent("ls").size());
    ASSERT_EQ(io->Stat("notes.txt", status), 0);
    EXPECT_EQ(status.size, 17u);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*io, "/bin/ls", content));
    EXPECT_EQ(content, BuiltinCommandFileContent("ls"));
    EXPECT_EQ(io->OpenFile("/bin/ls", kFileOpenWriteCreateTruncate, kFileCreateMode), nullptr);
    EXPECT_NE(io->RemoveFile("/bin/ls"), 0);
    EXPECT_NE(io->RemoveDirectory("/bin"), 0);
}

// Every descriptor a process holds is released when its program ends, before
// it reports finished -- here, a builtin's Run returning (of a stop).
TEST_F(BuiltinCommandsTest, ABuiltinsDescriptorsAreReleasedWhenItsCommandReturns) {
    BuiltinCommandHost host{os, /*pid=*/1, /*parentPid=*/1, "/wait"};
    StartProcessOptions options;
    options.stdIn = std::make_shared<Mocks::MockFileDescriptor>();
    options.stdOut = std::make_shared<Mocks::MockFileDescriptor>();
    options.stdErr = std::make_shared<Mocks::MockFileDescriptor>();
    auto process = BuiltinProcess::Create(host, factory->CreateEnvironment(),
        std::make_shared<WaitCommand>(), {}, "/", options);
    ASSERT_NE(process, nullptr);

    auto releases = std::make_shared<std::atomic<int>>(0);
    // Slots 0, 1 and 2 hold the standard streams, so the next free slot is 3.
    ASSERT_EQ(process->IO()->AddDescriptor(ReleaseCountingDescriptor::Create(releases)), 3);

    // The command is still waiting, so nothing may have been released yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(releases->load(), 0);

    process->TriggerStop();
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    // Already released at the moment WaitToFinish returned -- not after.
    EXPECT_EQ(releases->load(), 1);
}

// A builtin's exit code: nothing while the command is still running; the
// command's own result (modulo 256) once it has returned; 143 -- 128 +
// SIGTERM -- when it was asked to stop first.
TEST_F(BuiltinCommandsTest, ExitCodeIsEmptyUntilFinishedThenTheStatus) {
    BuiltinCommandHost host{os, /*pid=*/1, /*parentPid=*/1, "/wait"};
    StartProcessOptions options;
    options.stdIn = std::make_shared<Mocks::MockFileDescriptor>();
    options.stdOut = std::make_shared<Mocks::MockFileDescriptor>();
    options.stdErr = std::make_shared<Mocks::MockFileDescriptor>();
    auto waiting = BuiltinProcess::Create(host, factory->CreateEnvironment(),
        std::make_shared<WaitCommand>(), {}, "/", options);
    ASSERT_NE(waiting, nullptr);
    EXPECT_FALSE(waiting->ExitCode().has_value());

    waiting->TriggerStop();
    ASSERT_TRUE(waiting->WaitToFinish(kWaitMs));
    ASSERT_TRUE(waiting->ExitCode().has_value());
    EXPECT_EQ(*waiting->ExitCode(), 143);

    // The command's own result: ls reports 2 for what it cannot access, and a
    // successful pwd 0.
    int status = -1;
    Run("ls", {"/nope"}, &status);
    EXPECT_EQ(status, 2);
    Run("pwd", {}, &status);
    EXPECT_EQ(status, 0);
}

// The low 8 bits of whatever Run returned, as a shell takes them.
TEST_F(BuiltinCommandsTest, ExitCodeIsTheCommandStatusModulo256) {
    class FixedStatusCommand : public IBuiltinCommand {
    public:
        explicit FixedStatusCommand(int status) : m_status(status) {}
        std::string Name() const override { return "exiter"; }
        std::string Version() const override { return "1"; }
        const std::vector<BuiltinOption>& Options() const override { return m_options; }
        BuiltinHelp Help() const override { return BuiltinHelp{"exits with a fixed status", {"exiter"}, ""}; }
        int Run(BuiltinContext&) override { return m_status; }

    private:
        int m_status;
        std::vector<BuiltinOption> m_options;
    };

    for (const auto [given, expected] : {std::pair{-1, 255}, {256 + 7, 7}}) {
        BuiltinCommandHost host{os, /*pid=*/1, /*parentPid=*/1, "/exiter"};
        StartProcessOptions options;
        options.stdIn = std::make_shared<Mocks::MockFileDescriptor>();
        options.stdOut = std::make_shared<Mocks::MockFileDescriptor>();
        options.stdErr = std::make_shared<Mocks::MockFileDescriptor>();
        auto process = BuiltinProcess::Create(host, factory->CreateEnvironment(),
            std::make_shared<FixedStatusCommand>(given), {}, "/", options);
        ASSERT_NE(process, nullptr);
        ASSERT_TRUE(process->WaitToFinish(kWaitMs));
        ASSERT_TRUE(process->ExitCode().has_value());
        EXPECT_EQ(*process->ExitCode(), expected) << given;
    }
}

// --- echo ---

TEST_F(BuiltinCommandsTest, EchoPrintsItsArgumentsSeparatedBySpaces) {
    EXPECT_EQ(Run("echo", {"hello", "world"}), (Lines{"hello world"}));
    EXPECT_EQ(Run("echo", {}), (Lines{""}));
}

TEST_F(BuiltinCommandsTest, EchoOptions) {
    EXPECT_EQ(Run("echo", {"-n", "no", "newline"}), (Lines{"no newline"}));
    EXPECT_EQ(Run("echo", {"-e", "a\\tb\\nc"}), (Lines{"a\tb", "c"}));
    EXPECT_EQ(Run("echo", {"a\\nb"}), (Lines{"a\\nb"}));
    EXPECT_EQ(Run("echo", {"-e", "stop\\cignored", "too"}), (Lines{"stop"}));
    EXPECT_EQ(Run("echo", {"-e", "\\x41\\0102"}), (Lines{"AB"}));
    // Only leading words made of n, e and E are options.
    EXPECT_EQ(Run("echo", {"-x", "-n"}), (Lines{"-x -n"}));
    EXPECT_EQ(Run("echo", {"-ne", "x\\ty"}), (Lines{"x\ty"}));
    // --help counts only when it is the only argument.
    EXPECT_EQ(Run("echo", {"--help", "me"}), (Lines{"--help me"}));
}

// --- pwd ---

TEST_F(BuiltinCommandsTest, PwdPrintsTheWorkingDirectory) {
    EXPECT_EQ(Run("pwd", {}), (Lines{"/"}));
    EXPECT_EQ(Run("pwd", {"-P"}, nullptr, "/docs/sub"), (Lines{"/docs/sub"}));
    int status = 0;
    auto lines = Run("pwd", {"-x"}, &status);
    EXPECT_EQ(status, 1);
    EXPECT_TRUE(Contains(lines, "pwd: invalid option -- 'x'"));
    EXPECT_TRUE(Contains(lines, "Try 'pwd --help'"));
}

// --- cat ---

TEST_F(BuiltinCommandsTest, CatConcatenatesFiles) {
    EXPECT_EQ(Run("cat", {"/docs/a.md", "sub/b.md"}, nullptr, "/docs"), (Lines{"alphabravo!"}));
    EXPECT_EQ(Run("cat", {"notes.txt"}), (Lines{"one", "two", "", "", "\tthree"}));
}

TEST_F(BuiltinCommandsTest, CatOptions) {
    EXPECT_EQ(Run("cat", {"-n", "/notes.txt"}),
        (Lines{"     1\tone", "     2\ttwo", "     3\t", "     4\t", "     5\t\tthree"}));
    EXPECT_EQ(Run("cat", {"-b", "/notes.txt"}),
        (Lines{"     1\tone", "     2\ttwo", "", "", "     3\t\tthree"}));
    EXPECT_EQ(Run("cat", {"-s", "/notes.txt"}), (Lines{"one", "two", "", "\tthree"}));
    EXPECT_EQ(Run("cat", {"-A", "/notes.txt"}), (Lines{"one$", "two$", "$", "$", "^Ithree$"}));
    EXPECT_EQ(Run("cat", {"--show-ends", "--squeeze", "/notes.txt"}), (Lines{"one$", "two$", "$", "\tthree$"}));
}

TEST_F(BuiltinCommandsTest, CatReadsABuiltinsNote) {
    EXPECT_EQ(Run("cat", {"/bin/cat"}), (Lines{BuiltinCommandFileContent("cat")}));
}

TEST_F(BuiltinCommandsTest, CatReportsWhatItCannotRead) {
    int status = 0;
    auto lines = Run("cat", {"/missing", "/docs", "/docs/a.md"}, &status);
    EXPECT_EQ(status, 1);
    EXPECT_TRUE(Contains(lines, "cat: /missing: No such file or directory"));
    EXPECT_TRUE(Contains(lines, "cat: /docs: Is a directory"));
    EXPECT_TRUE(Contains(lines, "alpha"));

    lines = Run("cat", {}, &status);
    EXPECT_EQ(status, 1);
    EXPECT_TRUE(Contains(lines, "standard input is not supported"));
}

// --- mkdir ---

TEST_F(BuiltinCommandsTest, MkdirCreatesADirectory) {
    int status = -1;
    EXPECT_TRUE(Run("mkdir", {"new"}, &status, "/docs").empty());
    EXPECT_EQ(status, 0);
    EXPECT_EQ(EntryTypeOf(*root, "/docs/new"), std::optional<char>(DirectoryEntryType::Dir));
}

TEST_F(BuiltinCommandsTest, MkdirParentsAndVerbose) {
    int status = -1;
    auto lines = Run("mkdir", {"-pv", "x/y/z"}, &status);
    EXPECT_EQ(status, 0);
    EXPECT_EQ(lines, (Lines{
        "mkdir: created directory 'x'",
        "mkdir: created directory 'x/y'",
        "mkdir: created directory 'x/y/z'"}));
    EXPECT_EQ(EntryTypeOf(*root, "/x/y/z"), std::optional<char>(DirectoryEntryType::Dir));
    // -p: an existing directory is no error.
    EXPECT_TRUE(Run("mkdir", {"-p", "/x/y"}, &status).empty());
    EXPECT_EQ(status, 0);
}

TEST_F(BuiltinCommandsTest, MkdirReportsFailures) {
    int status = 0;
    auto lines = Run("mkdir", {"/docs", "/no/parent", "/notes.txt/sub", "/bin/echo"}, &status);
    EXPECT_EQ(status, 1);
    EXPECT_TRUE(Contains(lines, "mkdir: cannot create directory '/docs': File exists"));
    EXPECT_TRUE(Contains(lines, "mkdir: cannot create directory '/no/parent': No such file or directory"));
    EXPECT_TRUE(Contains(lines, "mkdir: cannot create directory '/notes.txt/sub': Not a directory"));
    EXPECT_TRUE(Contains(lines, "mkdir: cannot create directory '/bin/echo': File exists"));

    lines = Run("mkdir", {}, &status);
    EXPECT_EQ(status, 1);
    EXPECT_TRUE(Contains(lines, "mkdir: missing operand"));
    // Permissions do not exist yet: -m is taken, said to be not treated, and
    // the directory is made anyway.
    lines = Run("mkdir", {"-m", "755", "/m"}, &status);
    EXPECT_EQ(status, 0);
    EXPECT_EQ(lines, (Lines{"Parameter -m is not treated by HaisosOS mkdir v. 1.1.0"}));
    EXPECT_EQ(EntryTypeOf(*root, "/m"), std::optional<char>(DirectoryEntryType::Dir));
}

// --- ls ---

TEST_F(BuiltinCommandsTest, LsListsInColumnsByDefault) {
    EXPECT_EQ(Run("ls", {}), (Lines{"bin  docs  notes.txt"}));
    EXPECT_EQ(Run("ls", {"/bin"}), (Lines{"cat  echo  ls  mkdir  pwd"}));
    EXPECT_EQ(Run("ls", {"-1", "/bin"}), (Lines{"cat", "echo", "ls", "mkdir", "pwd"}));
    EXPECT_EQ(Run("ls", {"-r", "/bin"}), (Lines{"pwd  mkdir  ls  echo  cat"}));
}

TEST_F(BuiltinCommandsTest, LsWrapsColumnsTopToBottomAt80Characters) {
    for (int i = 0; i < 12; ++i) {
        const std::string name = "/wide/file_with_a_quite_long_name_" + std::string(1, static_cast<char>('a' + i));
        if (i == 0) {
            ASSERT_EQ(root->CreateDirectory("/wide", kDirMode), 0);
        }
        WriteFile(name, "");
    }
    auto lines = Run("ls", {"/wide"});
    // Names of 29 characters: two columns fit in 80, three do not.
    ASSERT_EQ(lines.size(), 6u);
    EXPECT_EQ(lines[0], "file_with_a_quite_long_name_a  file_with_a_quite_long_name_g");
    EXPECT_EQ(lines[5], "file_with_a_quite_long_name_f  file_with_a_quite_long_name_l");
}

TEST_F(BuiltinCommandsTest, LsHiddenEntries) {
    EXPECT_EQ(Run("ls", {"-a", "/"}), (Lines{".  ..  .hidden  bin  docs  notes.txt"}));
    EXPECT_EQ(Run("ls", {"-A", "/"}), (Lines{".hidden  bin  docs  notes.txt"}));
}

TEST_F(BuiltinCommandsTest, LsLongFormatIsTheRealOnes) {
    // "total" in 1K blocks; links, owner, group, size, time and name in columns.
    EXPECT_EQ(WithoutTimes(Run("ls", {"-l", "/docs"})), (Lines{
        "total 1",
        "-rwxrwxrwx 1 haisos haisos 5 <time> a.md",
        "drwxrwxrwx 2 haisos haisos 0 <time> sub"}));
    // File operands get no total line.
    const std::string noteSize = std::to_string(BuiltinCommandFileContent("cat").size());
    EXPECT_EQ(WithoutTimes(Run("ls", {"-l", "/bin/cat"})),
        (Lines{"-rwxrwxrwx 1 haisos haisos " + noteSize + " <time> /bin/cat"}));
}

TEST_F(BuiltinCommandsTest, LsLongFormatAlignsItsColumns) {
    WriteFile("/docs/big.bin", std::string(12345, 'x'));
    EXPECT_EQ(WithoutTimes(Run("ls", {"-l", "/docs"})), (Lines{
        "total 13",
        "-rwxrwxrwx 1 haisos haisos     5 <time> a.md",
        "-rwxrwxrwx 1 haisos haisos 12345 <time> big.bin",
        "drwxrwxrwx 2 haisos haisos     0 <time> sub"}));
    EXPECT_EQ(WithoutTimes(Run("ls", {"-lh", "/docs"})), (Lines{
        "total 13K",
        "-rwxrwxrwx 1 haisos haisos   5 <time> a.md",
        "-rwxrwxrwx 1 haisos haisos 13K <time> big.bin",
        "drwxrwxrwx 2 haisos haisos   0 <time> sub"}));
}

TEST_F(BuiltinCommandsTest, LsLongFormatVariants) {
    EXPECT_EQ(WithoutTimes(Run("ls", {"-g", "/docs/sub"})), (Lines{"total 1", "-rwxrwxrwx 1 haisos 6 <time> b.md"}));
    EXPECT_EQ(WithoutTimes(Run("ls", {"-o", "/docs/sub"})), (Lines{"total 1", "-rwxrwxrwx 1 haisos 6 <time> b.md"}));
    EXPECT_EQ(WithoutTimes(Run("ls", {"-lG", "/docs/sub"})), (Lines{"total 1", "-rwxrwxrwx 1 haisos 6 <time> b.md"}));
    EXPECT_EQ(WithoutTimes(Run("ls", {"-ls", "/docs/sub"})), (Lines{"total 1", "1 -rwxrwxrwx 1 haisos haisos 6 <time> b.md"}));
    // The directory's link count follows its subdirectories.
    auto lines = WithoutTimes(Run("ls", {"-ld", "/docs"}));
    EXPECT_EQ(lines, (Lines{"drwxrwxrwx 3 haisos haisos 0 <time> /docs"}));
}

TEST_F(BuiltinCommandsTest, LsLongFormatShowsARecentTimeAsTheClock) {
    // Everything here was just made, so it is shown as "Mon dd HH:MM", the
    // time being the one Stat reports.
    FileStatus status;
    ASSERT_EQ(root->Stat("/docs/a.md", status), 0);
    const std::time_t modified = static_cast<std::time_t>(status.modificationTime.seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &modified);
#else
    localtime_r(&modified, &local);
#endif
    char expected[32];
    std::strftime(expected, sizeof(expected), "%H:%M", &local);
    auto lines = Run("ls", {"-l", "/docs/a.md"});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_TRUE(std::regex_match(lines[0],
        std::regex("-rwxrwxrwx 1 haisos haisos 5 [A-Z][a-z]{2} [ 123][0-9] [0-9]{2}:[0-9]{2} /docs/a\\.md")))
        << lines[0];
    EXPECT_NE(lines[0].find(expected), std::string::npos) << lines[0] << " vs " << expected;
}

TEST_F(BuiltinCommandsTest, LsTimeStyles) {
    auto lines = Run("ls", {"--full-time", "/docs/a.md"});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_TRUE(std::regex_match(lines[0], std::regex(
        "-rwxrwxrwx 1 haisos haisos 5 [0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{9} [+-][0-9]{4} /docs/a\\.md")))
        << lines[0];
    lines = Run("ls", {"-l", "--time-style=long-iso", "/docs/a.md"});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_TRUE(std::regex_match(lines[0], std::regex(
        "-rwxrwxrwx 1 haisos haisos 5 [0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2} /docs/a\\.md"))) << lines[0];
    lines = Run("ls", {"-l", "--time-style=+%Y", "/docs/a.md"});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_TRUE(std::regex_match(lines[0], std::regex("-rwxrwxrwx 1 haisos haisos 5 [0-9]{4} /docs/a\\.md"))) << lines[0];

    int status = 0;
    lines = Run("ls", {"--time-style=bogus"}, &status);
    EXPECT_EQ(status, 2);
    EXPECT_TRUE(Contains(lines, "ls: invalid argument 'bogus' for '--time-style'"));
}

// A conversion strftime does not know is printed as written, as glibc prints
// one; on Windows, whose C runtime would end the whole program over it, ls
// writes it out before strftime sees it. %s, a GNU extension, is the time in
// seconds since the epoch everywhere.
TEST_F(BuiltinCommandsTest, LsTimeStyleWithAConversionStrftimeDoesNotKnow) {
    const auto lines = Run("ls", {"-l", "--time-style=+%Q|%s|%", "/docs/a.md"});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_TRUE(std::regex_match(lines[0], std::regex("-rwxrwxrwx 1 haisos haisos 5 %Q\\|[0-9]+\\|% /docs/a\\.md")))
        << lines[0];
}

#ifndef _WIN32
TEST_F(BuiltinCommandsTest, LsLongFormatShowsAnOldTimeWithItsYear) {
    // A real disk file, mounted in, dated 15 March 2020: older than six months,
    // so the year replaces the clock.
    const std::string hostDir = "/tmp/haisos_builtin_ls_old_time";
    std::filesystem::remove_all(hostDir);
    std::filesystem::create_directories(hostDir);
    std::ofstream(hostDir + "/old.txt") << "old!";
    std::tm when{};
    when.tm_year = 2020 - 1900;
    when.tm_mon = 2;
    when.tm_mday = 15;
    when.tm_hour = 12;
    when.tm_isdst = -1;
    const std::time_t stamp = std::mktime(&when);
    struct utimbuf times{stamp, stamp};
    ASSERT_EQ(::utime((hostDir + "/old.txt").c_str(), &times), 0);
    root->Mount("/disk", factory->CreatePhysicalFileSystem(hostDir));

    auto lines = Run("ls", {"-l", "/disk"});
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0].rfind("total ", 0), 0u) << lines[0];
    EXPECT_EQ(lines[1], "-rwxrwxrwx 1 haisos haisos 4 Mar 15  2020 old.txt");
    std::filesystem::remove_all(hostDir);
}
#endif

TEST_F(BuiltinCommandsTest, LsSortOrders) {
    WriteFile("/docs/big.bin", std::string(3000, 'x'));
    WriteFile("/docs/c.txt", "cc");
    EXPECT_EQ(Run("ls", {"-1S", "/docs"}), (Lines{"big.bin", "a.md", "c.txt", "sub"}));
    EXPECT_EQ(Run("ls", {"-1X", "/docs"}), (Lines{"sub", "big.bin", "a.md", "c.txt"}));
    EXPECT_EQ(Run("ls", {"-1r", "/docs"}), (Lines{"sub", "c.txt", "big.bin", "a.md"}));
    EXPECT_EQ(Run("ls", {"-1", "--group-directories-first", "/docs"}), (Lines{"sub", "a.md", "big.bin", "c.txt"}));
    // -t: newest first. c.txt is rewritten last, so it comes first.
    WriteFile("/docs/c.txt", "newer");
    auto byTime = Run("ls", {"-1t", "/docs"});
    ASSERT_FALSE(byTime.empty());
    EXPECT_EQ(byTime[0], "c.txt");
    int status = 0;
    auto lines = Run("ls", {"--sort=version", "-1", "/docs/sub"}, &status);
    EXPECT_EQ(status, 0);
    EXPECT_EQ(lines, (Lines{"Parameter --sort=version is not treated by HaisosOS ls v. 1.2.1", "b.md"}));
}

TEST_F(BuiltinCommandsTest, LsLayouts) {
    EXPECT_EQ(Run("ls", {"-m", "/bin"}), (Lines{"cat, echo, ls, mkdir, pwd"}));
    EXPECT_EQ(Run("ls", {"-m", "-w", "12", "/bin"}), (Lines{"cat, echo,", "ls, mkdir,", "pwd"}));
    EXPECT_EQ(Run("ls", {"-x", "-w", "16", "/bin"}), (Lines{"cat    echo  ls", "mkdir  pwd"}));
    // A line must stay shorter than the width: at 16 the three columns (16
    // characters) do not fit, at 17 they do.
    EXPECT_EQ(Run("ls", {"-C", "-w", "16", "/bin"}), (Lines{"cat   mkdir", "echo  pwd", "ls"}));
    EXPECT_EQ(Run("ls", {"-C", "-w", "17", "/bin"}), (Lines{"cat   ls     pwd", "echo  mkdir"}));
    EXPECT_EQ(Run("ls", {"-p", "/docs"}), (Lines{"a.md  sub/"}));
    EXPECT_EQ(Run("ls", {"-s", "/docs/sub"}), (Lines{"total 1", "1 b.md"}));
}

TEST_F(BuiltinCommandsTest, LsQuotesNamesAsTheRealOneDoes) {
    ASSERT_EQ(root->CreateDirectory("/q", kDirMode), 0);
    WriteFile("/q/plain", "");
    WriteFile("/q/with space", "");
    WriteFile("/q/it's", "");
    // Unquoted names shift right by one so every name lines up.
    EXPECT_EQ(Run("ls", {"/q"}), (Lines{"\"it's\"   plain  'with space'"}));
    EXPECT_EQ(Run("ls", {"-1", "/q"}), (Lines{"\"it's\"", "plain", "'with space'"}));
    EXPECT_EQ(Run("ls", {"-N", "/q"}), (Lines{"it's  plain  with space"}));
}

TEST_F(BuiltinCommandsTest, LsOfSeveralOperandsListsFilesFirstThenEachDirectory) {
    EXPECT_EQ(Run("ls", {"/docs", "/notes.txt", "/docs/sub"}), (Lines{
        "/notes.txt",
        "",
        "/docs:",
        "a.md  sub",
        "",
        "/docs/sub:",
        "b.md"}));
    EXPECT_EQ(Run("ls", {"-d", "/docs", "/bin"}), (Lines{"/bin  /docs"}));
}

TEST_F(BuiltinCommandsTest, LsRecursive) {
    EXPECT_EQ(Run("ls", {"-R"}, nullptr, "/docs"), (Lines{
        ".:",
        "a.md  sub",
        "",
        "./sub:",
        "b.md"}));
    EXPECT_EQ(WithoutTimes(Run("ls", {"-lR", "/docs"})), (Lines{
        "/docs:",
        "total 1",
        "-rwxrwxrwx 1 haisos haisos 5 <time> a.md",
        "drwxrwxrwx 2 haisos haisos 0 <time> sub",
        "",
        "/docs/sub:",
        "total 1",
        "-rwxrwxrwx 1 haisos haisos 6 <time> b.md"}));
}

TEST_F(BuiltinCommandsTest, LsShowsDotAndDotDotFirstOnlyWithAll) {
    EXPECT_EQ(Run("ls", {"-a", "/docs"}), (Lines{".  ..  a.md  sub"}));
    EXPECT_EQ(Run("ls", {"-f", "/docs/sub"}), (Lines{".  ..  b.md"}));
    EXPECT_EQ(Run("ls", {"-A", "/docs"}), (Lines{"a.md  sub"}));
    // "." is the directory itself and ".." its parent, as their link counts show.
    EXPECT_EQ(WithoutTimes(Run("ls", {"-la", "/docs"})), (Lines{
        "total 1",
        "drwxrwxrwx 3 haisos haisos 0 <time> .",
        "drwxrwxrwx 4 haisos haisos 0 <time> ..",
        "-rwxrwxrwx 1 haisos haisos 5 <time> a.md",
        "drwxrwxrwx 2 haisos haisos 0 <time> sub"}));
}

TEST_F(BuiltinCommandsTest, LsShowsDevicesAsTheRealOneDoes) {
    root->Mount("/dev", factory->CreateServicesCreator()->CreateFileSystemService()->CreateDeviceFileSystem());
    EXPECT_EQ(Run("ls", {"/dev"}), (Lines{"null  zero"}));
    EXPECT_EQ(Run("ls", {"-p", "/dev"}), (Lines{"null  zero"}));
    // A device's size column holds its major and minor numbers.
    EXPECT_EQ(WithoutTimes(Run("ls", {"-l", "/dev"})), (Lines{
        "total 0",
        "crwxrwxrwx 1 haisos haisos 1, 3 <time> null",
        "crwxrwxrwx 1 haisos haisos 1, 5 <time> zero"}));
    // Aligned with the sizes of files listed alongside.
    EXPECT_EQ(WithoutTimes(Run("ls", {"-l", "/docs/a.md", "/dev/null"})), (Lines{
        "crwxrwxrwx 1 haisos haisos 1, 3 <time> /dev/null",
        "-rwxrwxrwx 1 haisos haisos    5 <time> /docs/a.md"}));
    WriteFile("/docs/big.bin", std::string(123456, 'x'));
    EXPECT_EQ(WithoutTimes(Run("ls", {"-l", "/docs/big.bin", "/dev/zero"})), (Lines{
        "crwxrwxrwx 1 haisos haisos   1, 5 <time> /dev/zero",
        "-rwxrwxrwx 1 haisos haisos 123456 <time> /docs/big.bin"}));
}

TEST_F(BuiltinCommandsTest, CatOfDevNullPrintsNothing) {
    root->Mount("/dev", factory->CreateServicesCreator()->CreateFileSystemService()->CreateDeviceFileSystem());
    int status = -1;
    EXPECT_EQ(Run("cat", {"/dev/null"}, &status), (Lines{}));
    EXPECT_EQ(status, 0);
}

TEST_F(BuiltinCommandsTest, LsReportsWhatItCannotAccess) {
    int status = 0;
    auto lines = Run("ls", {"/missing", "/docs/a.md"}, &status);
    EXPECT_EQ(status, 2);
    EXPECT_TRUE(Contains(lines, "ls: cannot access '/missing': No such file or directory"));
    EXPECT_TRUE(Contains(lines, "/docs/a.md"));
    lines = Run("ls", {"--bogus"}, &status);
    EXPECT_EQ(status, 2);
    EXPECT_TRUE(Contains(lines, "ls: unrecognized option '--bogus'"));
    // Long options may be abbreviated, as with getopt_long.
    EXPECT_EQ(Run("ls", {"--rec", "/docs/sub"}), (Lines{"/docs/sub:", "b.md"}));
}

// --- stdio of a builtin ---

TEST_F(BuiltinCommandsTest, ErrorsGoToStderrOnly) {
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/ls", {"/nope"}, "/", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    EXPECT_EQ(console->TakeOut(), "");
    EXPECT_EQ(console->TakeErr(), "ls: cannot access '/nope': No such file or directory\n");
}

TEST_F(BuiltinCommandsTest, OutputIsRawAndUntagged) {
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/echo", {"-n", "abc"}, "/", StartProcessOptions{});
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    EXPECT_EQ(console->TakeOut(), "abc");
    process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/echo", {"hi"}, "/", StartProcessOptions{});
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    EXPECT_EQ(console->TakeOut(), "hi\n");
}

TEST_F(BuiltinCommandsTest, NotTreatedReportsGoToStderr) {
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/mkdir", {"-m", "755", "/m"}, "/", StartProcessOptions{});
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    EXPECT_EQ(console->TakeOut(), "");
    EXPECT_TRUE(console->TakeErr().find("Parameter -m is not treated by HaisosOS mkdir v. ") != std::string::npos);
}

TEST_F(BuiltinCommandsTest, GivenStdoutReceivesTheOutput) {
    auto file = root->OpenFile("/out.txt", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(file, nullptr);

    StartProcessOptions options;
    options.stdOut = file;
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/echo", {"hello"}, "/", options);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    file.reset();

    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/out.txt", content));
    EXPECT_EQ(content, "hello\n");
    // The console was bypassed entirely.
    EXPECT_EQ(console->TakeOut(), "");
}

TEST_F(BuiltinCommandsTest, SameDescriptorForStdoutAndStderr) {
    auto both = std::make_shared<Mocks::MockFileDescriptor>();
    StartProcessOptions options;
    options.stdOut = both;
    options.stdErr = both;
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/ls", {"/docs", "/nope"}, "/", options);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));

    const std::string written = both->Written();
    EXPECT_NE(written.find("ls: cannot access '/nope'"), std::string::npos) << written;
    EXPECT_NE(written.find("a.md"), std::string::npos) << written;
}

// --- BuiltinContext's output rule ---

TEST(BuiltinContextTest, TerminalStdoutIsWrittenAtOnce) {
    auto io = ProcessFileIO::Create({}, "/");
    auto in = std::make_shared<Mocks::MockFileDescriptor>(/*isTerminal=*/true);
    auto out = std::make_shared<Mocks::MockFileDescriptor>(/*isTerminal=*/true);
    auto err = std::make_shared<Mocks::MockFileDescriptor>(/*isTerminal=*/true);
    ASSERT_TRUE(io->InstallStandardStreams(in, out, err));
    FakeProcess process(io);
    auto echo = FindStandardCommand("echo");
    ASSERT_NE(echo, nullptr);
    std::atomic<bool> stop{false};
    {
        BuiltinContext context(process, *echo, {}, stop);
        context.Out("$ ");
        // Unbuffered: a partial line -- a prompt -- is out at once.
        EXPECT_EQ(out->Written(), "$ ");
        context.Error("e");
        EXPECT_EQ(err->Written(), "echo: e\n");
    }
}

TEST(BuiltinContextTest, OtherStdoutIsBufferedUntilFullOrEnd) {
    auto io = ProcessFileIO::Create({}, "/");
    auto in = std::make_shared<Mocks::MockFileDescriptor>();
    // One descriptor as both stdOut and stdErr, so the order across the two
    // streams can be observed.
    auto both = std::make_shared<Mocks::MockFileDescriptor>();
    ASSERT_TRUE(io->InstallStandardStreams(in, both, both));
    FakeProcess process(io);
    auto echo = FindStandardCommand("echo");
    ASSERT_NE(echo, nullptr);
    std::atomic<bool> stop{false};
    {
        BuiltinContext context(process, *echo, {}, stop);
        context.Out("x");
        // Not a terminal: buffered, nothing has been written yet.
        EXPECT_EQ(both->Written(), "");
        // An error flushes stdout first: "x" precedes the error line.
        context.Error("e");
        EXPECT_EQ(both->Written(), "xecho: e\n");
        // Reaching the block size writes it out without waiting for the end.
        context.Out(std::string(5000, 'y'));
        EXPECT_GE(both->Written().size(), 9u + kBuiltinOutBufferSize);
        context.Out("z");
    }
    // The destructor flushed what was left.
    EXPECT_EQ(both->Written().size(), 9u + 5000u + 1u);
}

// --- option parsing ---

TEST(BuiltinArgsTest, ParsesClustersLongOptionsArgumentsAndOperands) {
    const std::vector<BuiltinOption> options = {
        {'a', "all", 1},
        {'m', "mode", 2, BuiltinArgument::Required, "MODE"},
        {'l', "", 3},
        {0, "color", kBuiltinNotTreated, BuiltinArgument::Optional, "WHEN"},
        {'Z', "", kBuiltinNotTreated},
    };
    auto parsed = ParseBuiltinArgs({"x", "-al", "--mode=755", "y", "-m", "644", "--color", "--color=auto", "-Z", "--", "-l"}, options);
    ASSERT_TRUE(parsed.error.empty()) << parsed.error;
    ASSERT_EQ(parsed.options.size(), 7u);
    EXPECT_EQ(parsed.options[0].id, 1);
    EXPECT_EQ(parsed.options[1].id, 3);
    EXPECT_EQ(parsed.options[2].argument, "755");
    EXPECT_EQ(parsed.options[3].argument, "644");
    EXPECT_EQ(parsed.options[3].spelling, "-m");
    EXPECT_FALSE(parsed.options[4].hasArgument);
    EXPECT_EQ(parsed.options[5].argument, "auto");
    EXPECT_EQ(parsed.options[5].spelling, "--color");
    EXPECT_EQ(parsed.options[6].id, kBuiltinNotTreated);
    EXPECT_EQ(parsed.operands, (std::vector<std::string>{"x", "y", "-l"}));

    EXPECT_EQ(ParseBuiltinArgs({"-m"}, options).error, "option requires an argument -- 'm'");
    EXPECT_EQ(ParseBuiltinArgs({"--all=1"}, options).error, "option '--all' doesn't allow an argument");
    EXPECT_EQ(ParseBuiltinArgs({"-"}, options).operands, (std::vector<std::string>{"-"}));
    EXPECT_EQ(ParseBuiltinArgs({"--he"}, options).options[0].id, kBuiltinOptionHelp);

}
