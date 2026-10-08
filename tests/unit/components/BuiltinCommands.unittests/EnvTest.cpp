#include "BuiltinCommandsFixture.h"
#include "BuiltinRunProgram.h"
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Haisos;

namespace {

// An environment with the given variables (usually PATH included).
std::shared_ptr<IEnvironment> MakeEnv(const std::shared_ptr<IFactory>& factory,
                                      std::initializer_list<std::pair<std::string, std::string>> vars) {
    auto environment = factory->CreateEnvironment();
    for (const auto& [name, value] : vars) {
        environment->SetVariable(name, value);
    }
    return environment;
}

} // namespace

// --- option parsing: the "+" mode ---

// GNU's "+" getopt mode: with stopAtFirstOperand, the first argument that is
// not an option ("-" alone included) ends the options, and every argument
// from it on is an operand untouched.
TEST_F(BuiltinCommandsTest, ParseStopsAtFirstOperandWhenAsked) {
    const std::vector<BuiltinOption> options = {
        {'i', "ignore", 1, BuiltinArgument::None},
        {'u', "unset", 2, BuiltinArgument::Required, "NAME"},
    };
    auto parsed = ParseBuiltinArgs({"-i", "A=1", "-u", "x"}, options, /*stopAtFirstOperand=*/true);
    EXPECT_TRUE(parsed.error.empty()) << parsed.error;
    ASSERT_EQ(parsed.options.size(), 1u);
    EXPECT_EQ(parsed.options[0].id, 1);
    EXPECT_EQ(parsed.operands, (std::vector<std::string>{"A=1", "-u", "x"}));

    // Without the flag, "-u x" is an option.
    parsed = ParseBuiltinArgs({"-i", "A=1", "-u", "x"}, options);
    EXPECT_TRUE(parsed.error.empty()) << parsed.error;
    ASSERT_EQ(parsed.options.size(), 2u);
    EXPECT_EQ(parsed.options[1].argument, "x");
    EXPECT_EQ(parsed.operands, (std::vector<std::string>{"A=1"}));

    // A "--" before any operand still ends the options and is dropped;
    // a "-" alone is an operand even in "+" mode.
    parsed = ParseBuiltinArgs({"-i", "--", "A=1"}, options, /*stopAtFirstOperand=*/true);
    EXPECT_EQ(parsed.options.size(), 1u);
    EXPECT_EQ(parsed.operands, (std::vector<std::string>{"A=1"}));
    parsed = ParseBuiltinArgs({"-i", "-", "-u", "x"}, options, /*stopAtFirstOperand=*/true);
    EXPECT_EQ(parsed.options.size(), 1u);
    EXPECT_EQ(parsed.operands, (std::vector<std::string>{"-", "-u", "x"}));
}

// --- env ---

TEST_F(BuiltinCommandsTest, EnvPrintsSortedVariables) {
    // With no COMMAND, the variables are printed sorted by name.
    Captured captured = RunCaptured("env", {}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}, {"B", "2"}, {"A", "1"}}));
    EXPECT_EQ(captured.out, "A=1\nB=2\nPATH=/bin\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("env", {"-i", "A=1", "B=2"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "A=1\nB=2\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("env", {"-i", "-0", "A=1"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    // -0 ends each line with NUL: "A=1" plus one NUL byte, nothing more.
    EXPECT_EQ(captured.out, std::string("A=1\0", 4));
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("env", {"-u", "B"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}, {"B", "2"}, {"A", "1"}}));
    EXPECT_EQ(captured.out, "A=1\nPATH=/bin\n");
    EXPECT_EQ(captured.status, 0);

    // A first operand "-" means -i.
    captured = RunCaptured("env", {"-", "A=1"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "A=1\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, EnvRunsACommandWithTheEditedEnvironment) {
    Captured captured = RunCaptured("env",
        {"-i", "PATH=/bin", "A=1", "hsh", "-c", "echo $A"});
    EXPECT_EQ(captured.out, "1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The child env lists the edited environment, A=1 included.
    captured = RunCaptured("env", {"A=1", "env"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "A=1\nPATH=/bin\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, EnvReturnsTheExitCode) {
    Captured captured = RunCaptured("env", {"hsh", "-c", "exit 3"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.status, 3);
    captured = RunCaptured("env", {"false"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.status, 1);
}

// COMMAND is looked up in the *new* environment's PATH, as execvp does.
TEST_F(BuiltinCommandsTest, EnvLooksUpInTheNewPath) {
    Captured captured = RunCaptured("env", {"PATH=/bin", "echo", "hi"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/nowhere"}}));
    EXPECT_EQ(captured.out, "hi\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, EnvErrors) {
    Captured captured = RunCaptured("env", {"nothingx"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "env: 'nothingx': No such file or directory\n");
    EXPECT_EQ(captured.status, 127);

    // A directory named with a '/', and a file StartProcess refuses, are
    // both Permission denied.
    captured = RunCaptured("env", {"/docs"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.err, "env: '/docs': Permission denied\n");
    EXPECT_EQ(captured.status, 126);
    captured = RunCaptured("env", {"/notes.txt"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.err, "env: '/notes.txt': Permission denied\n");
    EXPECT_EQ(captured.status, 126);

    captured = RunCaptured("env", {"-C", "/nothing", "pwd"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.err, "env: cannot change directory to '/nothing': No such file or directory\n");
    EXPECT_EQ(captured.status, 125);

    captured = RunCaptured("env", {"-C", "/docs"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.err,
        "env: must specify command with --chdir (-C)\n"
        "Try 'env --help' for more information.\n");
    EXPECT_EQ(captured.status, 125);

    captured = RunCaptured("env", {"-0", "echo", "hi"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.err,
        "env: cannot specify --null (-0) with command\n"
        "Try 'env --help' for more information.\n");
    EXPECT_EQ(captured.status, 125);

    captured = RunCaptured("env", {"-u", "A=B", "true"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.err, "env: cannot unset 'A=B': Invalid argument\n");
    EXPECT_EQ(captured.status, 125);

    captured = RunCaptured("env", {"-i", "-u"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.err,
        "env: option requires an argument -- 'u'\n"
        "Try 'env --help' for more information.\n");
    EXPECT_EQ(captured.status, 125);
}

TEST_F(BuiltinCommandsTest, EnvChdir) {
    Captured captured = RunCaptured("env", {"-C", "/docs", "pwd"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "/docs\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, EnvSplitString) {
    Captured captured = RunCaptured("env", {"-S", "echo a  b \"c d\""}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "a b c d\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // \_ separates words outside quotes, a literal space inside them.
    captured = RunCaptured("env", {"-S", "echo \"x\\_y\""}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "x y\n");
    EXPECT_EQ(captured.status, 0);

    // # inside a word is literal; at the start of a word, a comment.
    captured = RunCaptured("env", {"-S", "echo a#b #c"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "a#b\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("env", {"-S", "'unterminated"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "env: no terminating quote in -S string\n");
    EXPECT_EQ(captured.status, 125);

    // GNU's messages for a bad escape and a backslash ending the string.
    captured = RunCaptured("env", {"-S", "echo \\q"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "env: invalid sequence '\\q' in -S\n");
    EXPECT_EQ(captured.status, 125);
    captured = RunCaptured("env", {"-S", "echo a\\"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "env: invalid backslash at end of string in -S\n");
    EXPECT_EQ(captured.status, 125);
}

// The caller's buffered stdout is flushed before the child starts, so output
// reaches the shared descriptor in order.
TEST_F(BuiltinCommandsTest, EnvFlushesBeforeTheChild) {
    const Captured captured = RunCaptured("hsh", {"-c", "env echo a; echo b"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "a\nb\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, EnvStopStopsTheChild) {
    auto process = os->StartProcess(MakeEnv(factory, {{"PATH", "/bin"}}),
        "/bin/env", {"sleep", "100"}, "/", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(3000));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(*process->ExitCode(), 143);
}