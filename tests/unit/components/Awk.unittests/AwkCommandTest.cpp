#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "commands/awk/AwkInvocation.h"

using namespace Haisos;

namespace {

// The not-implemented message every run of a program prints for now.
const char* kNotImplemented = "awk: running programs is not implemented yet\n";

} // namespace

class AwkCommandTest : public BuiltinCommandsTest {};

TEST_F(AwkCommandTest, VersionAndHelp) {
    const auto version = RunCaptured("awk", {"--version"});
    EXPECT_EQ(version.status, 0);
    EXPECT_EQ(version.out, "awk (HaisosOS builtin) 1.0.1\n");
    const auto shortVersion = RunCaptured("awk", {"-V"});
    EXPECT_EQ(shortVersion.status, 0);
    EXPECT_EQ(shortVersion.out, "awk (HaisosOS builtin) 1.0.1\n");

    const auto help = RunCaptured("awk", {"--help"});
    EXPECT_EQ(help.status, 0);
    const Lines lines = SplitLines(help.out);
    ASSERT_GT(lines.size(), 1u);
    EXPECT_EQ(lines[0], "HaisosOS awk version 1.0.1 - pattern scanning and processing language");
    EXPECT_EQ(lines[1], "Based on Linux gawk: https://man7.org/linux/man-pages/man1/gawk.1.html");

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
    // no not-treated report.
    const auto after = RunCaptured("awk", {"BEGIN{}", "-x", "--lint"});
    EXPECT_EQ(after.status, 2);
    EXPECT_EQ(after.out, "");
    EXPECT_EQ(after.err, kNotImplemented);

    const auto dashDash = RunCaptured("awk", {"--", "BEGIN{}", "-x"});
    EXPECT_EQ(dashDash.status, 2);
    EXPECT_EQ(dashDash.out, "");
    EXPECT_EQ(dashDash.err, kNotImplemented);
}

TEST_F(AwkCommandTest, NotTreatedOptionsAreReported) {
    const auto lint = RunCaptured("awk", {"--lint", "BEGIN{}"});
    EXPECT_EQ(lint.status, 2);
    EXPECT_NE(lint.err.find("Parameter --lint is not treated by HaisosOS awk v. 1.0.1"),
        std::string::npos) << lint.err;
    EXPECT_NE(lint.err.find(kNotImplemented), std::string::npos);

    // -P and --re-interval are treated: always on, so never reported.
    for (const std::string& option : {"-P", "--re-interval"}) {
        const auto treated = RunCaptured("awk", {option, "BEGIN{}"});
        EXPECT_EQ(treated.status, 2) << option;
        EXPECT_EQ(treated.err, kNotImplemented) << option;
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

    const auto file = RunCaptured("awk", {"-f", "/notes.txt"});
    EXPECT_EQ(file.status, 2);
    EXPECT_EQ(file.err, kNotImplemented);

    // -f - reads the program from standard input.
    const auto standardInput = RunCaptured("awk", {"-f", "-"}, "BEGIN{}");
    EXPECT_EQ(standardInput.status, 2);
    EXPECT_EQ(standardInput.err, kNotImplemented);
}