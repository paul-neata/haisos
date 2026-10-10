#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "commands/awk/AwkInvocation.h"

using namespace Haisos;

class AwkCommandTest : public BuiltinCommandsTest {};

TEST_F(AwkCommandTest, VersionAndHelp) {
    const auto version = RunCaptured("awk", {"--version"});
    EXPECT_EQ(version.status, 0);
    EXPECT_EQ(version.out, "awk (HaisosOS builtin) 1.5.0\n");
    const auto shortVersion = RunCaptured("awk", {"-V"});
    EXPECT_EQ(shortVersion.status, 0);
    EXPECT_EQ(shortVersion.out, "awk (HaisosOS builtin) 1.5.0\n");

    const auto help = RunCaptured("awk", {"--help"});
    EXPECT_EQ(help.status, 0);
    const Lines lines = SplitLines(help.out);
    ASSERT_GT(lines.size(), 1u);
    EXPECT_EQ(lines[0], "HaisosOS awk version 1.5.0 - pattern scanning and processing language");
    EXPECT_EQ(lines[1], "Based on Linux gawk: https://man7.org/linux/man-pages/man1/gawk.1.html");
    EXPECT_NE(help.out.find("(no \\y \\w \\s, no \\< \\>)"), std::string::npos) << help.out;

    // -h prints the same help, as gawk's.
    const auto shortHelp = RunCaptured("awk", {"-h"});
    EXPECT_EQ(shortHelp.status, 0);
    EXPECT_EQ(shortHelp.out, help.out);
}

TEST_F(AwkCommandTest, UsageErrors) {
    // No program at all.
    const auto none = RunCaptured("awk", {});
    EXPECT_EQ(none.status, 1);
    EXPECT_EQ(none.out, "");
    EXPECT_EQ(none.err, Awk::AwkUsageText());

    const auto invalid = RunCaptured("awk", {"-y"});
    EXPECT_EQ(invalid.status, 1);
    EXPECT_EQ(invalid.out, "");
    EXPECT_EQ(invalid.err, "awk: invalid option -- 'y'\n" + Awk::AwkUsageText());

    const auto missing = RunCaptured("awk", {"-f"});
    EXPECT_EQ(missing.status, 1);
    EXPECT_EQ(missing.out, "");
    EXPECT_EQ(missing.err, "awk: option requires an argument -- 'f'\n" + Awk::AwkUsageText());

    const auto assign = RunCaptured("awk", {"-v", "x", "BEGIN{}"});
    EXPECT_EQ(assign.status, 1);
    EXPECT_EQ(assign.out, "");
    EXPECT_EQ(assign.err, "awk: `x' argument to `-v' not in `var=value' form\n\n" + Awk::AwkUsageText());
}

TEST_F(AwkCommandTest, OptionsStopAtTheProgram) {
    // Everything after the program operand is an operand: no invalid option,
    // no not-treated report. BEGIN{} has no main or END item, so the input
    // (the operand files) is never read either.
    const auto after = RunCaptured("awk", {"BEGIN{}", "-x", "--lint"});
    EXPECT_EQ(after.status, 0);
    EXPECT_EQ(after.out, "");
    EXPECT_EQ(after.err, "");

    const auto dashDash = RunCaptured("awk", {"--", "BEGIN { print 42 }", "-x"});
    EXPECT_EQ(dashDash.status, 0);
    EXPECT_EQ(dashDash.out, "42\n");
    EXPECT_EQ(dashDash.err, "");
}

TEST_F(AwkCommandTest, NotTreatedOptionsAreReported) {
    const auto lint = RunCaptured("awk", {"--lint", "BEGIN{}"});
    EXPECT_EQ(lint.status, 0);
    EXPECT_NE(lint.err.find("Parameter --lint is not treated by HaisosOS awk v. 1.5.0"),
        std::string::npos) << lint.err;

    // -P and --re-interval are treated: always on, so never reported.
    for (const std::string& option : {"-P", "--re-interval"}) {
        const auto treated = RunCaptured("awk", {option, "BEGIN{}"});
        EXPECT_EQ(treated.status, 0) << option;
        EXPECT_EQ(treated.err, "") << option;
    }
}

TEST_F(AwkCommandTest, ProgramSources) {
    const auto missing = RunCaptured("awk", {"-f", "/nope.awk"});
    EXPECT_EQ(missing.status, 2);
    EXPECT_EQ(missing.out, "");
    EXPECT_EQ(missing.err,
        "awk: fatal: cannot open source file `/nope.awk' for reading: No such file or directory\n");

    const auto directory = RunCaptured("awk", {"-f", "/docs"});
    EXPECT_EQ(directory.status, 1);
    EXPECT_EQ(directory.out, "");
    EXPECT_EQ(directory.err, "awk: /docs:1: error: cannot read source file `/docs': Is a directory\n");

    // /notes.txt parses as three false patterns: it runs, nothing prints.
    const auto file = RunCaptured("awk", {"-f", "/notes.txt"});
    EXPECT_EQ(file.status, 0);
    EXPECT_EQ(file.out, "");
    EXPECT_EQ(file.err, "");

    // -f - reads the program from standard input.
    const auto standardInput = RunCaptured("awk", {"-f", "-"}, "BEGIN { print 7 }");
    EXPECT_EQ(standardInput.status, 0);
    EXPECT_EQ(standardInput.out, "7\n");
    EXPECT_EQ(standardInput.err, "");
}

TEST_F(AwkCommandTest, SyntaxErrorsAreReported) {
    // A syntax error in the program operand: gawk's report, status 1.
    const auto operand = RunCaptured("awk", {"BEGIN { x = = 1 }"});
    EXPECT_EQ(operand.status, 1);
    EXPECT_EQ(operand.out, "");
    EXPECT_EQ(operand.err,
        "awk: cmd. line:1: BEGIN { x = = 1 }\n"
        "awk: cmd. line:1:             ^ syntax error\n");

    // The end of a -f file inside a rule: gawk's (END OF FILE) report.
    WriteFile("/p.awk", "BEGIN {\n");
    const auto file = RunCaptured("awk", {"-f", "/p.awk"});
    EXPECT_EQ(file.status, 1);
    EXPECT_EQ(file.out, "");
    EXPECT_EQ(file.err,
        "awk: /p.awk:1: (END OF FILE)\n"
        "awk: /p.awk:1: ^ source files / command-line arguments must contain "
        "complete functions or rules\n");
}

TEST_F(AwkCommandTest, RunsBeginAndEnd) {
    // A program that parses runs: BEGIN and END both run, and with no
    // main item the input is never read.
    const auto parsed = RunCaptured("awk", {"BEGIN { print 1 }"}, "");
    EXPECT_EQ(parsed.status, 0);
    EXPECT_EQ(parsed.out, "1\n");
    EXPECT_EQ(parsed.err, "");

    // An action with no pattern runs on every record of the standard input.
    const auto whole = RunCaptured("awk", {"{ print }"}, "a\nb\n");
    EXPECT_EQ(whole.status, 0);
    EXPECT_EQ(whole.out, "a\nb\n");
    EXPECT_EQ(whole.err, "");
}