// The interactive shell: -i (or a terminal stdin with a terminal stderr)
// makes hsh prompt (PS1 "$ ", PS2 "> " while a command is incomplete), read
// its input line by line -- re-parsing the buffer of the pending command
// whole from its first line after each --, report errors with dash's line
// numbers without ending the shell, end on `exit`, and end at the end of the
// input with the last status, after a newline to stderr.
//
// The expected out/err bytes are dash's, checked against dash -i 0.5.12 with
// piped input: prompts and the final newline go to stderr, so they land among
// the diagnostics there.

#include "HshShellFixture.h"

#include "tests/mocks/MockFileDescriptor.h"

namespace Haisos {

TEST_F(HshShellTest, PromptsAndExit) {
    const Captured captured = RunCaptured("hsh", {"-i"}, "echo hi\nnosuch\nexit 4\n");
    EXPECT_EQ(captured.out, "hi\n");
    EXPECT_EQ(captured.err, "$ $ hsh: 2: nosuch: not found\n$ ");
    EXPECT_EQ(captured.status, 4);
}

TEST_F(HshShellTest, IncompleteCommandsShowPs2) {
    Captured captured = RunCaptured("hsh", {"-i"}, "if true\nthen echo t\nfi\n");
    EXPECT_EQ(captured.out, "t\n");
    EXPECT_EQ(captured.err, "$ > > $ \n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("hsh", {"-i"}, "cat <<E\nx\nE\n");
    EXPECT_EQ(captured.out, "x\n");
    EXPECT_EQ(captured.err, "$ > > $ \n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("hsh", {"-i"}, "echo a &&\necho b\n");
    EXPECT_EQ(captured.out, "a\nb\n");
    EXPECT_EQ(captured.err, "$ > $ \n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(HshShellTest, ErrorsDoNotEndTheShell) {
    Captured captured = RunCaptured("hsh", {"-i"}, "fi\necho after\n");
    EXPECT_EQ(captured.out, "after\n");
    EXPECT_EQ(captured.err, "$ hsh: 1: Syntax error: \"fi\" unexpected\n$ $ \n");
    EXPECT_EQ(captured.status, 0);

    // An expansion error ($? becomes 2) does not end the shell either.
    captured = RunCaptured("hsh", {"-i"}, "echo ${x?}\necho after\n");
    EXPECT_EQ(captured.out, "after\n");
    EXPECT_EQ(captured.err, "$ hsh: 1: x: parameter not set\n$ $ \n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(HshShellTest, LineNumbersCountEveryLine) {
    const Captured captured =
        RunCaptured("hsh", {"-i"}, "if true\nthen nosuch\nfi\nnosuch2\n");
    EXPECT_NE(captured.err.find("hsh: 2: nosuch: not found\n"), std::string::npos) << captured.err;
    EXPECT_NE(captured.err.find("hsh: 4: nosuch2: not found\n"), std::string::npos) << captured.err;
}

TEST_F(HshShellTest, EndOfInput) {
    // Ends with the last command's status.
    Captured captured = RunCaptured("hsh", {"-i"}, "false\n");
    EXPECT_EQ(captured.err, "$ $ \n");
    EXPECT_EQ(captured.status, 1);

    // A pending buffer is parsed once more as a script would be; what a
    // script would not accept gives its error, then the shell ends with 2.
    captured = RunCaptured("hsh", {"-i"}, "echo \"a\n");
    EXPECT_EQ(captured.err, "$ > hsh: 2: Syntax error: Unterminated quoted string\n$ \n");
    EXPECT_EQ(captured.status, 2);

    // Nothing at all: the one prompt, the newline, status 0.
    captured = RunCaptured("hsh", {"-i"}, "");
    EXPECT_EQ(captured.err, "$ \n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(HshShellTest, PromptsFollowPs1AndPs2) {
    const Captured captured = RunCaptured("hsh", {"-i"}, "PS1='% '; PS2='+ '\nif true\nthen :; fi\n");
    EXPECT_EQ(captured.err, "$ % + % \n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(HshShellTest, HeredocBodyErrorIsNotIncomplete) {
    // The heredoc is terminated (its delimiter line came), so a lexing error
    // in its body is a plain syntax error, not "more input needed": otherwise
    // "$ " would never follow "> " and the shell would hang in PS2 forever.
    const Captured captured =
        RunCaptured("hsh", {"-i"}, "cat <<E\n${x\nE\necho after\nexit\n");
    EXPECT_EQ(captured.out, "after\n");
    // The body sub-lexer consumes the trailing newline before it sees the
    // end of the body, so the error names line 3 (dash's own numbering
    // differs: it reports the end of the whole input).
    const std::string error = "hsh: 3: Syntax error: Missing '}'\n";
    const size_t at = captured.err.find(error);
    EXPECT_NE(at, std::string::npos) << captured.err;
    EXPECT_EQ(captured.err.find(error, at + 1), std::string::npos) << captured.err;
    // A PS1, not a stuck PS2, follows the error.
    EXPECT_EQ(captured.err.compare(at + error.size(), 2, "$ "), 0) << captured.err;
    EXPECT_EQ(captured.status, 0);
}

TEST_F(HshShellTest, ScenarioFour) {
    const std::string count = std::to_string(builtins->GetCommands().size());
    Captured captured = RunCaptured("hsh", {"-i"},
        "echo hi | wc -c\ncd /bin; ls | wc -l\nnosuch\nexit 4\n");
    EXPECT_EQ(captured.out, "3\n" + count + "\n");
    EXPECT_EQ(captured.err, "$ $ $ hsh: 3: nosuch: not found\n$ ");
    EXPECT_EQ(captured.status, 4);
}

TEST_F(HshShellTest, ATerminalStdinMakesItInteractive) {
    // No -i: a terminal stdin with a terminal stderr is interactive (dash's
    // rule). $- holds i (and s: the commands come from stdin), and the prompt
    // went to stderr.
    auto stdIn = std::make_shared<Mocks::MockFileDescriptor>(true);
    auto stdErr = std::make_shared<Mocks::MockFileDescriptor>(true);
    auto stdOut = std::make_shared<Mocks::MockFileDescriptor>(true);
    StartProcessOptions options;
    options.stdIn = stdIn;
    options.stdOut = stdOut;
    options.stdErr = stdErr;
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/hsh", {}, "/", options);
    ASSERT_NE(process, nullptr);
    stdIn->Feed("echo $-\n");
    stdIn->EndInput();
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    EXPECT_EQ(process->ExitCode().value_or(-1), 0);
    EXPECT_NE(stdOut->Written().find('i'), std::string::npos) << stdOut->Written();
    EXPECT_NE(stdOut->Written().find('s'), std::string::npos) << stdOut->Written();
    EXPECT_EQ(stdErr->Written().rfind("$ ", 0), 0u) << stdErr->Written();

    // stderr not a terminal: not interactive, whatever stdin is -- no i in $-
    // and nothing written to stderr.
    stdIn = std::make_shared<Mocks::MockFileDescriptor>(true);
    stdErr = std::make_shared<Mocks::MockFileDescriptor>(false);
    stdOut = std::make_shared<Mocks::MockFileDescriptor>(true);
    options.stdIn = stdIn;
    options.stdOut = stdOut;
    options.stdErr = stdErr;
    process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/hsh", {}, "/", options);
    ASSERT_NE(process, nullptr);
    stdIn->Feed("echo $-\n");
    stdIn->EndInput();
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    EXPECT_EQ(process->ExitCode().value_or(-1), 0);
    EXPECT_EQ(stdOut->Written().find('i'), std::string::npos) << stdOut->Written();
    EXPECT_EQ(stdErr->Written(), "");
}

TEST_F(HshShellTest, CommandStringIsNotInteractive) {
    // -c, with no terminal in sight: no option letters at all in $-.
    const Captured captured = RunCaptured("hsh", {"-c", "echo $-"});
    EXPECT_EQ(captured.out, "\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

} // namespace Haisos
