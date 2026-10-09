#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <vector>
#include "Factory.h"
#include "BuiltinCommand.h"
#include "BuiltinProcess.h"
#include "BuiltinCommandList.h"
#include "BuiltinCommandsFixture.h"
#include "ProcessFileIO.h"
#include "src/components/Filesystem/BuiltinCommandFile.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/libheaders/ExitCodes.h"
#include "tests/mocks/MockFileDescriptor.h"
#include "tests/mocks/ReleaseCountingDescriptor.h"

#ifndef _WIN32
#include <utime.h>
#endif

using namespace Haisos;

namespace {

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
    void StopForBrokenPipe() override { ++m_stopForBrokenPipeCount; }

    std::shared_ptr<IFileIO> IO() const override { return m_io; }
    std::shared_ptr<IAgent> AsAgent() override { return nullptr; }
    std::shared_ptr<IHaisosOS> OS() const override { return nullptr; }

    int StopForBrokenPipeCount() const { return m_stopForBrokenPipeCount.load(); }

private:
    std::shared_ptr<ProcessFileIO> m_io;
    std::atomic<int> m_stopForBrokenPipeCount{0};
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

} // namespace

// --- IBuiltinCommands ---

TEST_F(BuiltinCommandsTest, ListsEveryBuiltinSortedWithAVersion) {
    const auto commands = builtins->GetCommands();
    EXPECT_EQ(commands, (Lines{"[", "basename", "cat", "chmod", "cmp", "cp", "cut", "date", "dirname", "du", "echo",
        "egrep", "env", "false", "fgrep", "grep", "head", "hsh", "ls", "man", "mkdir", "mv", "nl", "printf", "pwd", "realpath", "rm",
        "rmdir", "seq", "sleep", "sort", "stat", "tail", "tee", "test", "touch", "tr", "true", "uniq", "wc", "which"}));
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
        // test takes no options: --help is an ordinary non-empty string
        // operand to it, as GNU's test, so running it shows nothing. Its
        // help has the same shape anyway -- and [ , its other name, is the
        // one of the pair that runs --help.
        Lines help;
        int status = 0;
        if (name == "test") {
            help = SplitLines(BuiltinHelpText(*command));
        } else {
            help = Run(name, {"--help"}, &status);
        }
        // GNU's false exits 1 even after --help; every other builtin 0.
        EXPECT_EQ(status, name == "false" ? 1 : 0) << name;
        ASSERT_GE(help.size(), 5u) << name;
        EXPECT_EQ(help[0].rfind("HaisosOS " + name + " version " + command->Version() + " - ", 0), 0u) << help[0];
        const std::string& real = command->Help().basedOn.empty() ? name : command->Help().basedOn;
        EXPECT_EQ(help[1], "Based on Linux " + real + ": https://man7.org/linux/man-pages/man1/" + real + ".1.html");
        EXPECT_EQ(help[2], "");
        EXPECT_EQ(help[3].rfind("Usage: " + name, 0), 0u) << help[3];
        EXPECT_TRUE(Contains(help, "      --help"));
        EXPECT_TRUE(Contains(help, "      --version"));

        // The last line lists every option Haisos does not treat, and only those.
        const std::string& last = help.back();
        ASSERT_EQ(last.rfind("Not treated arguments: ", 0), 0u) << name << ": " << last;
        bool anyNotTreated = false;
        for (const auto& option : command->Options()) {
            if (option.hidden) {
                continue;  // an obsolete spelling: parsed, never documented
            }
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

TEST_F(BuiltinCommandsTest, EveryBuiltinsManPageIsItsHelp) {
    for (const auto& command : CreateStandardBuiltinCommands()) {
        if (command->Name() == "hsh") {
            continue;  // hsh overrides ManPage(): its page is not its help
        }
        EXPECT_EQ(command->ManPage(), BuiltinHelpText(*command)) << command->Name();
        EXPECT_EQ(command->ManPage().back(), '\n') << command->Name();
    }
}

TEST_F(BuiltinCommandsTest, EveryBuiltinHasAVersion) {
    for (const auto& name : builtins->GetCommands()) {
        if (name == "test") {
            continue;  // --version is a non-empty string operand to test, as
                       // GNU's; [ --version is the pair's version line
        }
        int status = -1;
        auto version = Run(name, {"--version"}, &status);
        // GNU's false exits 1 even after --version; every other builtin 0.
        EXPECT_EQ(status, name == "false" ? 1 : 0) << name;
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

TEST_F(BuiltinCommandsTest, CatReadsStandardInputWithoutFile) {
    const Captured captured = RunCaptured("cat", {}, "piped\nlines\n");
    EXPECT_EQ(captured.out, "piped\nlines\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, CatReadsStandardInputForDash) {
    // A second '-' reads on from where the first stopped: at the end of the
    // input, so it adds nothing.
    const Captured captured = RunCaptured("cat", {"-", "/docs/a.md", "-"}, "x\n");
    EXPECT_EQ(captured.out, "x\nalpha");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, CatNumbersAcrossStdinAndFiles) {
    const Captured captured = RunCaptured("cat", {"-n", "-", "/notes.txt"}, "in\n");
    EXPECT_EQ(captured.out, "     1\tin\n     2\tone\n     3\ttwo\n     4\t\n     5\t\n     6\t\tthree\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

// With no input given, stdin ends at once, as /dev/null's does: nothing out.
TEST_F(BuiltinCommandsTest, CatWithNoInputPrintsNothing) {
    const Captured captured = RunCaptured("cat", {});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, CatReportsWhatItCannotRead) {
    int status = 0;
    auto lines = Run("cat", {"/missing", "/docs", "/docs/a.md"}, &status);
    EXPECT_EQ(status, 1);
    EXPECT_TRUE(Contains(lines, "cat: /missing: No such file or directory"));
    EXPECT_TRUE(Contains(lines, "cat: /docs: Is a directory"));
    EXPECT_TRUE(Contains(lines, "alpha"));
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
    // /five, not /bin: its listing does not change as builtins are added.
    MakeFiveNames();
    EXPECT_EQ(Run("ls", {"/five"}), (Lines{"cat  echo  ls  mkdir  pwd"}));
    EXPECT_EQ(Run("ls", {"-1", "/five"}), (Lines{"cat", "echo", "ls", "mkdir", "pwd"}));
    EXPECT_EQ(Run("ls", {"-r", "/five"}), (Lines{"pwd  mkdir  ls  echo  cat"}));
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
// one, everywhere -- BuiltinDate's FormatDateTime never hands one to the C
// library. %s, a GNU extension, is the time in seconds since the epoch
// everywhere.
TEST_F(BuiltinCommandsTest, LsTimeStyleWithAConversionStrftimeDoesNotKnow) {
    const auto lines = Run("ls", {"-l", "--time-style=+%Q|%s|%", "/docs/a.md"});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_TRUE(std::regex_match(lines[0], std::regex("-rwxrwxrwx 1 haisos haisos 5 %Q\\|[0-9]+\\|% /docs/a\\.md")))
        << lines[0];
}

// --time-style=+FORMAT takes GNU's flags: '-' does not pad, '_' pads with
// spaces, so the day of month and the month come out as ls's own styles write
// them too.
TEST_F(BuiltinCommandsTest, LsTimeStyleTakesGnuFlags) {
    const auto lines = Run("ls", {"-l", "--time-style=+%-d.%_m.%Y", "/docs/a.md"});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_TRUE(std::regex_match(lines[0], std::regex(
        "-rwxrwxrwx 1 haisos haisos 5 [1-9][0-9]?\\.[ 1][0-9]\\.[0-9]{4} /docs/a\\.md")))
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
    EXPECT_EQ(lines, (Lines{"Parameter --sort=version is not treated by HaisosOS ls v. 1.3.1", "b.md"}));
}

TEST_F(BuiltinCommandsTest, LsLayouts) {
    MakeFiveNames();
    EXPECT_EQ(Run("ls", {"-m", "/five"}), (Lines{"cat, echo, ls, mkdir, pwd"}));
    EXPECT_EQ(Run("ls", {"-m", "-w", "12", "/five"}), (Lines{"cat, echo,", "ls, mkdir,", "pwd"}));
    EXPECT_EQ(Run("ls", {"-x", "-w", "16", "/five"}), (Lines{"cat    echo  ls", "mkdir  pwd"}));
    // A line must stay shorter than the width: at 16 the three columns (16
    // characters) do not fit, at 17 they do.
    EXPECT_EQ(Run("ls", {"-C", "-w", "16", "/five"}), (Lines{"cat   mkdir", "echo  pwd", "ls"}));
    EXPECT_EQ(Run("ls", {"-C", "-w", "17", "/five"}), (Lines{"cat   ls     pwd", "echo  mkdir"}));
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

TEST_F(BuiltinCommandsTest, LsOnATerminalQuotesAsGnuShellEscape) {
    ASSERT_EQ(root->CreateDirectory("/q2", kDirMode), 0);
    for (const char* name : {"nl\ny", "a]b", "a{b", "{", "a=b", "#h", "a#", "tab\tt"}) {
        WriteFile(std::string("/q2/") + name, "");
    }
    // Sorted by bytes: '#' 0x23 < 'a' < 'n' < 't' < '{'; ']' and '{' elsewhere
    // need no quoting, '#' only as the first byte.
    EXPECT_EQ(Run("ls", {"-1", "/q2"}), (Lines{
        "'#h'", "a#", "'a=b'", "a]b", "a{b", "'nl'$'\\n''y'", "'tab'$'\\t''t'", "'{'"}));
    // -N on a terminal: literal names, control bytes shown as '?' (GNU's -q
    // default on a terminal).
    EXPECT_EQ(Run("ls", {"-N", "-1", "/q2"}), (Lines{
        "#h", "a#", "a=b", "a]b", "a{b", "nl?y", "tab?t", "{"}));
}

// GNU ls off a terminal (a pipe, a file, a device): one name per line unless
// a layout is asked for, and names literal, quoted never -- a control byte
// goes out raw.
TEST_F(BuiltinCommandsTest, LsIntoAPipeListsOneNamePerLine) {
    MakeFiveNames();
    Captured captured = RunCaptured("ls", {"/five"});
    EXPECT_EQ(captured.out, "cat\necho\nls\nmkdir\npwd\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    // A layout given wins over the one-per-line default.
    EXPECT_EQ(RunCaptured("ls", {"-C", "/five"}).out, "cat  echo  ls  mkdir  pwd\n");
    EXPECT_EQ(RunCaptured("ls", {"-x", "-w", "16", "/five"}).out, "cat    echo  ls\nmkdir  pwd\n");
    EXPECT_EQ(RunCaptured("ls", {"-m", "/five"}).out, "cat, echo, ls, mkdir, pwd\n");
    EXPECT_EQ(RunCaptured("ls", {"-r", "/five"}).out, "pwd\nmkdir\nls\necho\ncat\n");
}

TEST_F(BuiltinCommandsTest, LsIntoAPipeDoesNotQuote) {
    ASSERT_EQ(root->CreateDirectory("/q", kDirMode), 0);
    WriteFile("/q/plain", "");
    WriteFile("/q/with space", "");
    WriteFile("/q/it's", "");
    WriteFile("/q/ctl\x01x", "");
    EXPECT_EQ(RunCaptured("ls", {"/q"}).out, "ctl\x01x\nit's\nplain\nwith space\n");
    // Off a terminal nothing is quoted, so nothing is shifted.
    EXPECT_EQ(RunCaptured("ls", {"-C", "/q"}).out, "ctl\x01x  it's  plain  with space\n");
    // The long listing is the same one, with literal names.
    EXPECT_EQ(WithoutTimes(SplitLines(RunCaptured("ls", {"-l", "/q"}).out)), (Lines{
        "total 0",
        "-rwxrwxrwx 1 haisos haisos 0 <time> ctl\x01x",
        "-rwxrwxrwx 1 haisos haisos 0 <time> it's",
        "-rwxrwxrwx 1 haisos haisos 0 <time> plain",
        "-rwxrwxrwx 1 haisos haisos 0 <time> with space"}));
}

// GNU: "-1 has no effect after -l" -- and none before it either.
TEST_F(BuiltinCommandsTest, LsOnePerLineAfterLongKeepsLong) {
    EXPECT_EQ(WithoutTimes(Run("ls", {"-l1", "/docs"})), WithoutTimes(Run("ls", {"-l", "/docs"})));
    EXPECT_EQ(WithoutTimes(Run("ls", {"-1l", "/docs"})), WithoutTimes(Run("ls", {"-l", "/docs"})));
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

// --- a write into a pipe nobody reads ---

TEST_F(BuiltinCommandsTest, EchoIntoAPipeWithNoReaderExits141Quietly) {
    auto ends = os->GetPipeService()->CreatePipe();
    // The read end released before the start: nobody can ever read this pipe.
    ends.readEnd.reset();
    StartProcessOptions options;
    options.stdOut = ends.writeEnd;
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/echo", {"hello"}, "/", options);
    ends.writeEnd.reset();
    options.stdOut.reset();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(*process->ExitCode(), kExitCodeBrokenPipe);
    EXPECT_EQ(console->TakeOut(), "");
    EXPECT_EQ(console->TakeErr(), "");
}

// ls reports its failure on stderr -- a broken stderr is as fatal as a broken
// stdout, as SIGPIPE does not care which descriptor the write was on.
TEST_F(BuiltinCommandsTest, ABrokenStderrAlsoExits141Quietly) {
    auto ends = os->GetPipeService()->CreatePipe();
    ends.readEnd.reset();
    StartProcessOptions options;
    options.stdOut = ends.writeEnd;
    options.stdErr = ends.writeEnd;
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/ls", {"/nope"}, "/", options);
    ends.writeEnd.reset();
    options.stdOut.reset();
    options.stdErr.reset();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    // 141, not ls's 2 for what it cannot access.
    EXPECT_EQ(*process->ExitCode(), kExitCodeBrokenPipe);
    // Quietly: not even the diagnostic went anywhere -- it was dropped once
    // the write failed, so the pipe took nothing and the console neither.
    EXPECT_EQ(console->TakeOut(), "");
    EXPECT_EQ(console->TakeErr(), "");
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

// A broken pipe stops the process once -- on the first failed write -- and
// everything after is dropped, stderr included: the program is dying quietly.
TEST(BuiltinContextTest, ABrokenStdoutStopsTheProcessOnceAndDropsTheRest) {
    auto io = ProcessFileIO::Create({}, "/");
    auto in = std::make_shared<Mocks::MockFileDescriptor>();
    // Terminal, so Out writes at once rather than buffering.
    auto out = std::make_shared<Mocks::MockFileDescriptor>(/*isTerminal=*/true);
    auto err = std::make_shared<Mocks::MockFileDescriptor>(/*isTerminal=*/true);
    out->SetWriteResult(kIOBrokenPipe);
    ASSERT_TRUE(io->InstallStandardStreams(in, out, err));
    FakeProcess process(io);
    auto echo = FindStandardCommand("echo");
    ASSERT_NE(echo, nullptr);
    std::atomic<bool> stop{false};
    {
        BuiltinContext context(process, *echo, {}, stop);
        context.Out("a");
        context.Out("b");
        context.Error("e");
    }
    EXPECT_EQ(process.StopForBrokenPipeCount(), 1);
    // One write was tried; "b" and the diagnostic never reached a descriptor.
    EXPECT_EQ(out->WriteCalls(), 1);
    EXPECT_EQ(err->Written(), "");
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

// man-db's -T/-H/-X kind: an argument taken only attached ("-Tutf8",
// "--troff-device=utf8"), never from the next word.
TEST(BuiltinArgsTest, OptionalAttachedTakesOnlyAnAttachedArgument) {
    const std::vector<BuiltinOption> options = {
        {'T', "troff-device", 1, BuiltinArgument::OptionalAttached, "DEVICE"},
        {'F', "classify", 2, BuiltinArgument::Optional, "WHEN"},
        {'l', "", 3},
    };
    auto parsed = ParseBuiltinArgs({"-Tutf8", "-T", "x", "--troff-device=a", "--troff-device", "-Fl"}, options);
    ASSERT_TRUE(parsed.error.empty()) << parsed.error;
    EXPECT_EQ(parsed.operands, (std::vector<std::string>{"x"}));
    ASSERT_EQ(parsed.options.size(), 6u);
    EXPECT_EQ(parsed.options[0].id, 1);
    EXPECT_TRUE(parsed.options[0].hasArgument);
    EXPECT_EQ(parsed.options[0].argument, "utf8");
    EXPECT_EQ(parsed.options[1].id, 1);
    EXPECT_FALSE(parsed.options[1].hasArgument);
    EXPECT_EQ(parsed.options[2].id, 1);
    EXPECT_TRUE(parsed.options[2].hasArgument);
    EXPECT_EQ(parsed.options[2].argument, "a");
    EXPECT_EQ(parsed.options[3].id, 1);
    EXPECT_FALSE(parsed.options[3].hasArgument);
    EXPECT_EQ(parsed.options[4].id, 2);
    EXPECT_FALSE(parsed.options[4].hasArgument);
    EXPECT_EQ(parsed.options[5].id, 3);
    EXPECT_FALSE(parsed.options[5].hasArgument);
}

// The quoting GNU's shell-escape styles give (checked against GNU ls 9.4
// --quoting-style=shell-escape), byte for byte.
TEST(BuiltinArgsTest, ShellEscapeQuotedFollowsGnu) {
    // No shell would read anything in these specially: left as they are.
    EXPECT_EQ(ShellEscapeQuoted("plain"), "plain");
    EXPECT_EQ(ShellEscapeQuoted("a]b"), "a]b");
    EXPECT_EQ(ShellEscapeQuoted("a{b"), "a{b");
    EXPECT_EQ(ShellEscapeQuoted("a#"), "a#");
    EXPECT_EQ(ShellEscapeQuoted("a~"), "a~");
    EXPECT_EQ(ShellEscapeQuoted("x%y"), "x%y");
    EXPECT_EQ(ShellEscapeQuoted("a,b-c.d/e:f@g_h"), "a,b-c.d/e:f@g_h");

    // Quoted, no ' and no control byte: 'name'.
    EXPECT_EQ(ShellEscapeQuoted(""), "''");
    EXPECT_EQ(ShellEscapeQuoted("with space"), "'with space'");
    EXPECT_EQ(ShellEscapeQuoted("#h"), "'#h'");
    EXPECT_EQ(ShellEscapeQuoted("~x"), "'~x'");
    EXPECT_EQ(ShellEscapeQuoted("{"), "'{'");
    EXPECT_EQ(ShellEscapeQuoted("}"), "'}'");
    EXPECT_EQ(ShellEscapeQuoted("a=b"), "'a=b'");

    // A ' but nothing else double quotes would read specially: "name".
    EXPECT_EQ(ShellEscapeQuoted("it's"), "\"it's\"");
    EXPECT_EQ(ShellEscapeQuoted("it's x"), "\"it's x\"");

    // Otherwise: '...', each ' written '\'', each run of control bytes out of
    // the quotes as $'...' escapes.
    EXPECT_EQ(ShellEscapeQuoted("it's $x"), "'it'\\''s $x'");
    EXPECT_EQ(ShellEscapeQuoted("nl\ny"), "'nl'$'\\n''y'");
    EXPECT_EQ(ShellEscapeQuoted("\nb"), "''$'\\n''b'");
    EXPECT_EQ(ShellEscapeQuoted("a\x01\x02\x62"), "'a'$'\\001\\002''b'");
    EXPECT_EQ(ShellEscapeQuoted("x\x7fy"), "'x'$'\\177''y'");
    EXPECT_EQ(ShellEscapeQuoted("tab\tt"), "'tab'$'\\t''t'");
    EXPECT_EQ(ShellEscapeQuoted("a\x01"), "'a'$'\\001'");

    // always: quoted even when nothing asks for it.
    EXPECT_EQ(ShellEscapeQuoted("plain", true), "'plain'");
    EXPECT_EQ(ShellEscapeQuoted("it's", true), "\"it's\"");
}
