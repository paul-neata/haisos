#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "HshShellFixture.h"
#include "tests/mocks/MockFileDescriptor.h"

namespace Haisos {
TEST_F(HshShellTest, SimpleCommands) {
    const ShellCase cases[] = {
        {"echo hi", "hi\n"},
        {"echo a  b \"c  d\"", "a b c  d\n"},
        {"/bin/echo x", "x\n"},
        {"x=1; echo $x", "1\n"},
        {"x=a y=b; echo $x$y", "ab\n"},
        {":"},
        {"true"},
        {"false", "", "", 1},
        {"false; echo $?", "1\n"},
        {""},
        {"echo /docs/*", "/docs/a.md /docs/sub\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "SimpleCommands");
    }
}

TEST_F(HshShellTest, NotFoundAndNotRunnable) {
    const ShellCase cases[] = {
        {"nosuch", "", "hsh: 1: nosuch: not found\n", 127},
        {"/nope/x", "", "hsh: 1: /nope/x: not found\n", 127},
        {"/docs", "", "hsh: 1: /docs: Permission denied\n", 126},
        {"/notes.txt", "", "hsh: 1: /notes.txt: Permission denied\n", 126},
        {"echo a; nosuch; echo $?", "a\n127\n", "hsh: 1: nosuch: not found\n", 0},
        {"echo a\nnosuch", "a\n", "hsh: 2: nosuch: not found\n", 127},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "NotFoundAndNotRunnable");
    }
}

TEST_F(HshShellTest, PathLookup) {
    ExpectSh({"echo $PATH",
        "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\n"}, "PathLookup");
    const ShellCase cases[] = {
        {"PATH=/docs; echo x", "", "hsh: 1: echo: not found\n", 127},
        {"PATH=; echo x", "", "hsh: 1: echo: not found\n", 127},
        {"PATH=/nowhere:/bin; echo y", "y\n"},
        {"../bin/echo z", "z\n", "", 0, {}, std::nullopt, "/docs"},
        {"echo *", "a.md sub\n", "", 0, {}, std::nullopt, "/docs"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "PathLookup");
    }
}

TEST_F(HshShellTest, Lists) {
    const ShellCase cases[] = {
        {"true && echo a", "a\n"},
        {"false && echo a", "", "", 1},
        {"false || echo b", "b\n"},
        {"true || echo b"},
        {"echo 1 && false || echo 2", "1\n2\n"},
        {"! true; echo $?", "1\n"},
        {"! false; echo $?", "0\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "Lists");
    }
}

TEST_F(HshShellTest, EnvironmentOfChildren) {
    os->GetOsEnvironment()->SetVariable("GREETING", "hi");
    const ShellCase cases[] = {
        // An imported variable is exported: a child shell sees it.
        {"echo $GREETING; hsh -c 'echo $GREETING'", "hi\nhi\n"},
        // A shell variable that was never exported does not reach children.
        {"y=2; hsh -c 'echo \"[$y]\"'", "[]\n"},
        // A prefix assignment reaches its command only, not the shell after.
        {"x=1 hsh -c 'echo $x'; echo \"[$x]\"", "1\n[]\n"},
        {"hsh -c 'echo $PWD'", "/docs\n", "", 0, {}, std::nullopt, "/docs"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "EnvironmentOfChildren");
    }
}

TEST_F(HshShellTest, BuiltinAssignments) {
    // A special builtin's prefix assignments stay; a regular one's are temporary.
    const ShellCase cases[] = {
        {"x=1 :; echo $x", "1\n"},
        {"x=1 true; echo \"[$x]\"", "[]\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "BuiltinAssignments");
    }
}

TEST_F(HshShellTest, StartupVariables) {
    const ShellCase cases[] = {
        {"echo \"$PS1|$PS2|$PS4|$OPTIND\"", "$ |> |+ |1\n"},
        {"echo \"[$IFS]\"", "[ \t\n]\n"},
        {"echo $0 $#", "hsh 0\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "StartupVariables");
    }
}

TEST_F(HshShellTest, Exit) {
    const ShellCase cases[] = {
        {"exit 3", "", "", 3},
        {"false; exit", "", "", 1},
        {"exit 256", "", "", 0},
        {"exit 1 2", "", "", 1},
        {"echo a; exit 4; echo b", "a\n", "", 4},
        {"exit abc", "", "hsh: 1: exit: Illegal number: abc\n", 2},
        {"exit -1", "", "hsh: 1: exit: Illegal number: -1\n", 2},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "Exit");
    }
}

TEST_F(HshShellTest, FatalErrors) {
    const ShellCase cases[] = {
        // An expansion error is fatal to a non-interactive shell: nothing after runs.
        {"echo ${x?}; echo after", "", "hsh: 1: x: parameter not set\n", 2},
        {"echo ${x;}", "", "hsh: 1: Bad substitution\n", 2},
        // A syntax error is found before anything of its line runs.
        {"echo a; fi", "", "hsh: 1: Syntax error: \"fi\" unexpected\n", 2},
        // Line 1 ran; line 2 fails to parse; line 3 never runs.
        {"echo a\nfi\necho b", "a\n", "hsh: 2: Syntax error: \"fi\" unexpected\n", 2},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "FatalErrors");
    }
}

TEST_F(HshShellTest, Invocation) {
    auto captured = RunCaptured("hsh", {"-c", "echo $0 $1 $#", "nm", "a", "b"});
    EXPECT_EQ(captured.out, "nm a 2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // --help anywhere but first is an ordinary argument, as for dash (quoted
    // here: bare, the "echo --help" it expands to would print echo's help).
    captured = RunCaptured("hsh", {"-c", "echo \"[$0]\"", "--help"});
    EXPECT_EQ(captured.out, "[--help]\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("hsh", {"-y"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "hsh: 0: Illegal option -y\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("hsh", {"-m", "-c", "echo ok"});
    EXPECT_EQ(captured.out, "ok\n");
    EXPECT_EQ(captured.err, NotTreatedLine("-m"));
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("hsh", {"--version"});
    EXPECT_EQ(captured.out, BuiltinVersionText(*CreateHshCommand()));
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("hsh", {"--help"});
    EXPECT_EQ(captured.out, BuiltinHelpText(*CreateHshCommand()));
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A script: $0 is its path, messages start with it, and each line's
    // commands run before the next line is parsed.
    WriteFile("/s.sh", "echo $0 $1\nnosuch\n");
    captured = RunCaptured("hsh", {"/s.sh", "p"});
    EXPECT_EQ(captured.out, "/s.sh p\n");
    EXPECT_EQ(captured.err, "/s.sh: 2: nosuch: not found\n");
    EXPECT_EQ(captured.status, 127);

    captured = RunCaptured("hsh", {"/nonexist.sh"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "hsh: 0: cannot open /nonexist.sh: No such file\n");
    EXPECT_EQ(captured.status, 2);

    // No arguments: commands come from standard input.
    captured = RunCaptured("hsh", {}, "echo in $#\n");
    EXPECT_EQ(captured.out, "in 0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("hsh", {"-s", "a1"}, "echo $1\n");
    EXPECT_EQ(captured.out, "a1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(HshShellTest, LuaChildren) {
    WriteFile("/e.lua", "exit(3)");
    ExpectSh({"/e.lua; echo $?", "3\n"}, "LuaChildren");

    WriteFile("/p.lua", "print(\"to stdout\")\nerror(\"boom\")\n");
    ExpectSh({"/p.lua; echo $?", "to stdout\n1\n", "lua: /p.lua:2: boom\n", 0}, "LuaChildren");
}

TEST_F(HshShellTest, StopEndsTheShellAndItsChild) {
    WriteFile("/spin.lua", "while true do end");
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/hsh",
        {"-c", "/spin.lua"}, "/", StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(10000));
    EXPECT_EQ(process->ExitCode().value_or(-1), 143);
    // The child went with it. The OS prunes finished processes from its list
    // when a next process starts, so poll with a trivial start until the
    // child's path is gone (up to 5 s: a child the shell left running would
    // stay listed).
    bool childGone = false;
    for (int attempts = 0; attempts < 50 && !childGone; ++attempts) {
        auto probe = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/pwd", {}, "/", StartProcessOptions{});
        ASSERT_NE(probe, nullptr);
        EXPECT_TRUE(probe->WaitToFinish(kWaitMs));
        childGone = true;
        for (const auto& p : os->GetRunningProcesses()) {
            if (p->Path() == "/spin.lua") {
                childGone = false;
            }
        }
        if (!childGone) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    EXPECT_TRUE(childGone);
}

TEST_F(HshShellTest, BrokenPipeOnItsOwnStderr) {
    // The shell's stderr is a pipe nobody reads: its first diagnostic hits the
    // broken pipe and the shell ends quietly, as SIGPIPE would, with 141.
    auto ends = os->GetPipeService()->CreatePipe();
    ends.readEnd.reset();
    auto stdOut = std::make_shared<Mocks::MockFileDescriptor>();
    StartProcessOptions options;
    options.stdOut = stdOut;
    options.stdErr = ends.writeEnd;
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/hsh",
        {"-c", "nosuch; /bin/echo after"}, "/", options);
    ASSERT_NE(process, nullptr);
    ends.writeEnd.reset();  // the test's copy: the process holds the only one
    EXPECT_TRUE(process->WaitToFinish(kWaitMs));
    EXPECT_EQ(process->ExitCode().value_or(-1), 141);
    EXPECT_EQ(stdOut->Written(), "");
}

} // namespace Haisos
