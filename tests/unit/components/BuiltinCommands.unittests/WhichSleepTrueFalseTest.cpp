#include "BuiltinCommandsFixture.h"
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

// --- which ---

TEST_F(BuiltinCommandsTest, WhichFindsInPath) {
    auto env = MakeEnv(factory, {{"PATH", "/bin"}});
    Captured captured = RunCaptured("which", {"ls"}, std::nullopt, "/", env);
    EXPECT_EQ(captured.out, "/bin/ls\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("which", {"-a", "ls"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin:/bin"}}));
    EXPECT_EQ(captured.out, "/bin/ls\n/bin/ls\n");
    EXPECT_EQ(captured.status, 0);

    // One miss among hits still prints the hit, exit 1.
    captured = RunCaptured("which", {"nothingx", "ls"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "/bin/ls\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);

    // No operands at all: exit 1, nothing printed.
    captured = RunCaptured("which", {}, std::nullopt, "/", MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);

    // -s prints nothing, exit status only.
    captured = RunCaptured("which", {"-s", "ls"}, std::nullopt, "/", MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A name with a '/' is reported as it is, when a file is there.
    captured = RunCaptured("which", {"/bin/ls"}, std::nullopt, "/", MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "/bin/ls\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("which", {"/docs"}, std::nullopt, "/", MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 1);

    // An empty PATH entry means '.': the candidate is built literally.
    captured = RunCaptured("which", {"ls"}, std::nullopt, "/bin",
        MakeEnv(factory, {{"PATH", ":/bin"}}));
    EXPECT_EQ(captured.out, "./ls\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, WhichBadOption) {
    const Captured captured = RunCaptured("which", {"-x", "ls"}, std::nullopt, "/",
        MakeEnv(factory, {{"PATH", "/bin"}}));
    EXPECT_EQ(captured.out, "Usage: /bin/which [-as] args\n");
    EXPECT_EQ(captured.err, "Illegal option -x\n");
    EXPECT_EQ(captured.status, 2);
}

// --- sleep ---

TEST_F(BuiltinCommandsTest, SleepSumsAndValidates) {
    Captured captured = RunCaptured("sleep", {"0.05", "0.01s", "0m", "0h", "0d"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("sleep", {"x", "y", "0"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
        "sleep: invalid time interval 'x'\n"
        "sleep: invalid time interval 'y'\n"
        "Try 'sleep --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    // What follows NUMBER must be nothing or exactly one suffix.
    captured = RunCaptured("sleep", {"1x"});
    EXPECT_EQ(captured.err,
        "sleep: invalid time interval '1x'\n"
        "Try 'sleep --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("sleep", {"0 "});
    EXPECT_EQ(captured.err,
        "sleep: invalid time interval '0 '\n"
        "Try 'sleep --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
    // strtod skips leading whitespace.
    captured = RunCaptured("sleep", {" 0"});
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("sleep", {});
    EXPECT_EQ(captured.err,
        "sleep: missing operand\n"
        "Try 'sleep --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, SleepIsStoppedPromptly) {
    auto process = os->StartProcess(MakeEnv(factory, {{"PATH", "/bin"}}),
        "/bin/sleep", {"inf"}, "/", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(1000));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(*process->ExitCode(), 143);
}

// --- true and false ---

TEST_F(BuiltinCommandsTest, TrueAndFalse) {
    // Every argument is ignored, options included.
    Captured captured = RunCaptured("true", {"-x", "--bogus"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("false", {"x"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);

    // --help/--version count only as the sole argument; false exits 1 even
    // after them, as GNU's does. `man false` prints the same page.
    captured = RunCaptured("false", {"--help"});
    EXPECT_EQ(captured.out, RunCaptured("man", {"false"}).out);
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("true", {"--help", "x"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("true", {"--version"});
    EXPECT_EQ(captured.out, "true (HaisosOS builtin) 1.0.0\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("false", {"--version"});
    EXPECT_EQ(captured.out, "false (HaisosOS builtin) 1.0.0\n");
    EXPECT_EQ(captured.status, 1);
}