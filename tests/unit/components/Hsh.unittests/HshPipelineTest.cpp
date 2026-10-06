#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "HshShellFixture.h"
#include "tests/mocks/MockFileDescriptor.h"

namespace Haisos {

namespace {

// Polls until a /spin.lua shows among the OS's running processes (bounded to
// 5 s): how a test knows a spinning child has actually started before it
// stops the shell, without a fixed sleep a slow runner can overrun.
bool RunningSpinCountAtLeast(IHaisosOS& os, size_t howMany) {
    for (int attempts = 0; attempts < 50; ++attempts) {
        size_t count = 0;
        for (const auto& process : os.GetRunningProcesses()) {
            if (process->Path() == "/spin.lua") {
                ++count;
            }
        }
        if (count >= howMany) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

// Polls (starting a trivial process each round, so the OS prunes the finished
// ones) until no /spin.lua is listed any more, bounded to 5 s.
void ExpectNoSpinLeft(IHaisosOS& os) {
    bool spinGone = false;
    for (int attempts = 0; attempts < 50 && !spinGone; ++attempts) {
        auto probe = os.StartProcess(os.GetOsEnvironment()->Clone(), "/bin/pwd", {}, "/", StartProcessOptions{});
        ASSERT_NE(probe, nullptr);
        EXPECT_TRUE(probe->WaitToFinish(kWaitMs));
        spinGone = true;
        for (const auto& p : os.GetRunningProcesses()) {
            if (p->Path() == "/spin.lua") {
                spinGone = false;
            }
        }
        if (!spinGone) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    EXPECT_TRUE(spinGone);
}

} // namespace

TEST_F(HshShellTest, PipelinesOfChildren) {
    const ShellCase cases[] = {
        {"echo hi | wc -c", "3\n"},
        {"cat /notes.txt | cat | wc -l", "5\n"},
        {"ls /docs | wc -l", "2\n"},
        {"echo a | cat > /p.txt; cat /p.txt", "a\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "PipelinesOfChildren");
    }
}

TEST_F(HshShellTest, PipelineStatus) {
    WriteFile("/e.lua", "exit(3)");
    const ShellCase cases[] = {
        {"false | true; echo $?", "0\n"},
        {"true | false; echo $?", "1\n"},
        {"! true | false; echo $?", "0\n"},
        // The stage that could not start has the 127 of its report; the
        // pipeline's status is its last stage's.
        {"nosuch | wc -c; echo $?", "0\n0\n", "hsh: 1: nosuch: not found\n"},
        {"/e.lua | cat; echo $?", "0\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "PipelineStatus");
    }
}

TEST_F(HshShellTest, InShellStagesAreSubshells) {
    const ShellCase cases[] = {
        // A stage inside the shell runs in a subshell: nothing it changes
        // (a variable, exit) leaks into the shell.
        {"x=1; true | x=2; echo $x", "1\n"},
        {"exit 3 | cat; echo $?", "0\n"},
        {"true | exit 4; echo $?", "4\n"},
        {"echo b | x=2; echo \"[$x]\"", "[]\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "InShellStagesAreSubshells");
    }
}

TEST_F(HshShellTest, NoDeadlockBetweenInShellStages) {
    WriteFile("/big.txt", std::string(200000, 'x'));
    const ShellCase cases[] = {
        // The in-shell stage reads nothing: the writer (200000 bytes, past the
        // 64 KiB of a bounded pipe) gets a broken pipe and stops.
        {"cat /big.txt | : | wc -c", "0\n"},
        {"cat /big.txt | cat | wc -c", "200000\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "NoDeadlockBetweenInShellStages");
    }
}

TEST_F(HshShellTest, BackgroundAndWait) {
    WriteFile("/e.lua", "exit(3)");
    const ShellCase cases[] = {
        {"/bin/echo bg > /bg.txt & wait $!; echo $?; cat /bg.txt", "0\nbg\n"},
        {"/e.lua & wait $!; echo $?", "3\n"},
        {"/e.lua & wait; echo $?", "0\n"},
        {"echo $!", "\n"},
        {"wait 99999; echo $?", "127\n"},
        {"wait abc; echo $?", "2\n", "hsh: 1: wait: Illegal number: abc\n"},
        {"wait %1; echo $?", "2\n", "hsh: 1: wait: No such job: %1\n"},
        {"true & echo $?", "0\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "BackgroundAndWait");
    }
}

TEST_F(HshShellTest, BackgroundStdinIsEmpty) {
    // An asynchronous list's stdin is empty: `cat` reads nothing of the
    // shell's own input.
    ExpectSh({"cat & wait", "", ""}, "BackgroundStdinIsEmpty");
    const Captured captured = Sh("cat & wait", {}, "typed\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(HshShellTest, BackgroundShellCommands) {
    // An and-or list (or a builtin) needs the shell itself: a child hsh runs
    // the list's text, at the same time as the shell goes on.
    const ShellCase cases[] = {
        {"true && /bin/echo bg > /b.txt & wait $!; echo $?; cat /b.txt", "0\nbg\n"},
        {"exit 3 & wait $!; echo $?", "3\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "BackgroundShellCommands");
    }

    // The words are expanded in the child, which sees only exported
    // variables: y came from the environment (imported exported), x did not.
    os->GetOsEnvironment()->SetVariable("y", "2");
    ExpectSh({"x=1; true && echo \"[$x][$y]\" & wait", "[][2]\n"}, "BackgroundShellCommands");

    // $0 and the positional parameters are passed on.
    ExpectSh({"true && echo $0 $1 $# & wait", "nm a 2\n", "", 0, {"nm", "a", "b"}},
        "BackgroundShellCommands");

    // The child runs in the shell's working directory.
    ExpectSh({"true && pwd & wait", "/docs\n", "", 0, {}, std::nullopt, "/docs"},
        "BackgroundShellCommands");

    // -u reaches the child (its errors are its own, with its own line
    // numbers); plain `wait` then ends 0 whatever the child did.
    const Captured nounset = RunCaptured("hsh", {"-u", "-c", "true && echo ${nope} & wait"});
    EXPECT_EQ(nounset.out, "");
    EXPECT_NE(nounset.err.find("nope: parameter not set"), std::string::npos) << nounset.err;
    EXPECT_EQ(nounset.status, 0);
}

TEST_F(HshShellTest, BackgroundShellCommandsRunConcurrently) {
    WriteFile("/spin.lua", "while true do end");
    auto stdOut = std::make_shared<Mocks::MockFileDescriptor>();
    StartProcessOptions options;
    options.stdOut = stdOut;
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/hsh",
        {"-c", "true && /spin.lua & echo started; wait"}, "/", options);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(RunningSpinCountAtLeast(*os, 1));
    // "started" is written while the background list's spin still runs.
    bool started = false;
    for (int attempts = 0; attempts < 50 && !started; ++attempts) {
        started = stdOut->Written().find("started\n") != std::string::npos;
        if (!started) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    EXPECT_TRUE(started);
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(10000));
    EXPECT_EQ(process->ExitCode().value_or(-1), 143);
    ExpectNoSpinLeft(*os);
}

TEST_F(HshShellTest, BackgroundPrelude) {
    // The options that are on among e u f x C a reach the child (as an
    // invocation argument, in OptionLetters' order): here -e and -x.
    Captured captured = RunCaptured("hsh", {"-e", "-x", "-c", "true && echo $- & wait"});
    EXPECT_EQ(captured.out, "xe\n");
    EXPECT_EQ(captured.status, 0);

    // None on: nothing to pass on.
    captured = RunCaptured("hsh", {"-c", "true && echo \"[$-]\" & wait"});
    EXPECT_EQ(captured.out, "[]\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(HshShellTest, CommandSubstitution) {
    WriteFile("/abc.txt", "one\ntwo\nthree\n");
    WriteFile("/e.lua", "exit(3)");
    const ShellCase cases[] = {
        {"echo $(echo a)", "a\n"},
        {"x=$(echo a; echo b); echo \"[$x]\"", "[a\nb]\n"},
        {"echo `echo b`", "b\n"},
        {"x=$(exit 5); echo $? \"[$x]\"", "5 []\n"},
        // A command not found inside is reported by the subshell; the status
        // seen by $?, then the outer echo ends 0.
        {"echo $(nosuch); echo $?", "\n0\n", "hsh: 1: nosuch: not found\n"},
        {"x=$(cat /abc.txt | wc -l); echo $x", "3\n"},
        {"echo $(echo $(echo deep))", "deep\n"},
        // What the substitution's subshell changes does not leak.
        {"x=out; y=$(x=inner; echo $x); echo $x $y", "out inner\n"},
        // Inner empty lines are kept; only the trailing newlines are dropped.
        {"echo \"$(cat /notes.txt)\"", "one\ntwo\n\n\n\tthree\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "CommandSubstitution");
    }
}

TEST_F(HshShellTest, CommandSubstitutionInAStageIsWaitedFor) {
    WriteFile("/e.lua", "exit(3)");
    const ShellCase cases[] = {
        // The words of a child stage (started without waiting) hold a
        // substitution: its commands still run one after another, each
        // waited for, so $? inside it is /e.lua's.
        {"/bin/echo $(/e.lua; echo $?) | cat", "3\n"},
        {"/bin/echo $(/e.lua; echo $?) > /s.txt & wait; cat /s.txt", "3\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "CommandSubstitutionInAStageIsWaitedFor");
    }
}

TEST_F(HshShellTest, CommandSubstitutionBigOutput) {
    WriteFile("/big.txt", std::string(200000, 'x'));
    // Past a bounded pipe's 64 KiB, so only the unbounded pipe lets this run.
    ExpectSh({"x=$(cat /big.txt); echo ${#x}", "200000\n"}, "CommandSubstitutionBigOutput");
}

TEST_F(HshShellTest, AcceptanceScenarioOne) {
    WriteFile("/abc.txt", "one\ntwo\nthree\n");
    const Captured captured =
        Sh("cat /abc.txt | wc -l; ls /bin | wc -l > /n.txt; cat /n.txt");
    EXPECT_EQ(captured.out, "3\n" + std::to_string(builtins->GetCommands().size()) + "\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(HshShellTest, AcceptanceScenarioFive) {
    WriteFile("/p.lua", "print(\"to stdout\")\nerror(\"boom\")\n");
    const Captured captured = Sh("/p.lua 2>/e.txt | wc -l; cat /e.txt");
    EXPECT_EQ(captured.out, "1\nlua: /p.lua:2: boom\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(HshShellTest, StopEndsEveryStage) {
    WriteFile("/spin.lua", "while true do end");
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/hsh",
        {"-c", "/spin.lua | /spin.lua"}, "/", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(RunningSpinCountAtLeast(*os, 2));
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(10000));
    EXPECT_EQ(process->ExitCode().value_or(-1), 143);
    ExpectNoSpinLeft(*os);
}

} // namespace Haisos
