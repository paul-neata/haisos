#include "BuiltinCommandsFixture.h"

using namespace Haisos;

namespace {

// man-db's "%-20s - %s": "<name> (1)" left-aligned in 20 columns.
std::string WhatisLine(const std::string& shown, const std::string& summary) {
    std::string line = shown + " (1)";
    if (line.size() < 20) {
        line.append(20 - line.size(), ' ');
    }
    return line + " - " + summary + "\n";
}

const std::string kLsLine = WhatisLine("ls", "list directory contents");
const std::string kWcLine = WhatisLine("wc", "print newline, word, and byte counts for each file");
const std::string kCatLine = WhatisLine("cat", "concatenate files and print on the standard output");

} // namespace

TEST_F(BuiltinCommandsTest, ManPrintsEachBuiltinsHelp) {
    for (const auto& name : builtins->GetCommands()) {
        if (name == "hsh") {
            continue;  // hsh overrides ManPage(): its page is not its help
        }
        if (name == "test") {
            continue;  // test takes no options: `test --help` is a true
                       // expression that prints nothing; `man test` is the
                       // pair's help, as `[ --help` is
        }
        const Captured man = RunCaptured("man", {name});
        const Captured help = RunCaptured(name, {"--help"});
        EXPECT_EQ(man.out, help.out) << name;
        EXPECT_EQ(man.err, "") << name;
        EXPECT_EQ(man.status, 0) << name;
    }
}

TEST_F(BuiltinCommandsTest, ManShowsTheShellsFullPage) {
    // `man hsh` prints the shell's full manual page, not its --help text.
    const std::string page = CreateHshCommand()->ManPage();
    const Captured man = RunCaptured("man", {"hsh"});
    EXPECT_EQ(man.out, page);
    EXPECT_EQ(man.err, "");
    EXPECT_EQ(man.status, 0);
    const Captured help = RunCaptured("hsh", {"--help"});
    EXPECT_GT(SplitLines(page).size(), SplitLines(help.out).size());

    // Every section heading, at column 0, in order.
    const std::vector<std::string> headings = {
        "NAME", "SYNOPSIS", "DESCRIPTION", "OPTIONS", "QUOTING AND ESCAPING",
        "PARAMETERS AND EXPANSIONS", "PIPELINES", "REDIRECTIONS",
        "HERE-DOCUMENTS (HEREDOCS)", "LISTS", "GROUPS AND SUBSHELLS", "IF",
        "WHILE AND UNTIL", "FOR", "CASE", "FUNCTIONS", "BUILTIN COMMANDS",
        "EXIT STATUS", "DIFFERENCES FROM DASH", "SEE ALSO",
    };
    const Lines lines = SplitLines(page);
    size_t next = 0;
    for (const std::string& heading : headings) {
        while (next < lines.size() && lines[next] != heading) {
            ++next;
        }
        ASSERT_LT(next, lines.size()) << heading;
        ++next;
    }
    for (const std::string& line : lines) {
        EXPECT_LE(line.size(), 79u) << line;
        if (line.find("Example: ") != std::string::npos) {
            EXPECT_EQ(line.rfind("       Example: ", 0), 0u) << line;
        }
    }

    // The DIFFERENCES FROM DASH list covers the -ef approximation, and the
    // --help notes name the prompt variables.
    bool inDifferences = false;
    bool efMentioned = false;
    for (const std::string& line : lines) {
        if (line == "DIFFERENCES FROM DASH") {
            inDifferences = true;
        } else if (line == "SEE ALSO") {
            inDifferences = false;
        } else if (inDifferences && line.find("-ef") != std::string::npos) {
            efMentioned = true;
        }
    }
    EXPECT_TRUE(efMentioned);
    EXPECT_NE(help.out.find("PS1, PS2 and PS4 are not expanded"), std::string::npos);
}

TEST_F(BuiltinCommandsTest, ManPrintsSeveralPagesBackToBack) {
    const std::string lsHelp = RunCaptured("ls", {"--help"}).out;
    const std::string wcHelp = RunCaptured("wc", {"--help"}).out;
    const Captured captured = RunCaptured("man", {"ls", "wc"});
    EXPECT_EQ(captured.out, lsHelp + wcHelp);
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, ManReportsAMissingPage) {
    Captured captured = RunCaptured("man", {"nosuch"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "No manual entry for nosuch\n");
    EXPECT_EQ(captured.status, 16);

    // A found page still prints, and 16 says one was missing.
    captured = RunCaptured("man", {"ls", "nosuch"});
    EXPECT_EQ(captured.out, RunCaptured("ls", {"--help"}).out);
    EXPECT_EQ(captured.err, "No manual entry for nosuch\n");
    EXPECT_EQ(captured.status, 16);

    captured = RunCaptured("man", {"n1", "n2"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "No manual entry for n1\nNo manual entry for n2\n");
    EXPECT_EQ(captured.status, 16);
}

TEST_F(BuiltinCommandsTest, ManTakesASectionFirst) {
    const std::string lsHelp = RunCaptured("ls", {"--help"}).out;

    // Every page is in section 1, and only the exact section 1 finds it.
    Captured captured = RunCaptured("man", {"1", "ls"});
    EXPECT_EQ(captured.out, lsHelp);
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("man", {"8", "ls"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "No manual entry for ls in section 8\n");
    EXPECT_EQ(captured.status, 16);

    captured = RunCaptured("man", {"1", "nosuch"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "No manual entry for nosuch in section 1\n");
    EXPECT_EQ(captured.status, 16);

    captured = RunCaptured("man", {"1x", "ls"});
    EXPECT_EQ(captured.err, "No manual entry for ls in section 1x\n");
    EXPECT_EQ(captured.status, 16);

    captured = RunCaptured("man", {"3posix", "ls"});
    EXPECT_EQ(captured.err, "No manual entry for ls in section 3posix\n");
    EXPECT_EQ(captured.status, 16);

    // "10" is not a section (its second character is a digit): a page name.
    captured = RunCaptured("man", {"10", "ls"});
    EXPECT_EQ(captured.out, lsHelp);
    EXPECT_EQ(captured.err, "No manual entry for 10\n");
    EXPECT_EQ(captured.status, 16);
}

TEST_F(BuiltinCommandsTest, ManWithoutAPage) {
    Captured captured = RunCaptured("man", {});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "What manual page do you want?\nFor example, try 'man man'.\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("man", {"1"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
        "No manual entry for 1\n"
        "(Alternatively, what manual page do you want from section 1?)\n"
        "For example, try 'man man'.\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, ManIgnoresCaseUnlessAskedNotTo) {
    const std::string lsHelp = RunCaptured("ls", {"--help"}).out;
    EXPECT_EQ(RunCaptured("man", {"LS"}).out, lsHelp);

    Captured captured = RunCaptured("man", {"-I", "LS"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "No manual entry for LS\n");
    EXPECT_EQ(captured.status, 16);

    // The last of -i/-I wins.
    captured = RunCaptured("man", {"-I", "-i", "LS"});
    EXPECT_EQ(captured.out, lsHelp);
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, ManWhatis) {
    Captured captured = RunCaptured("man", {"-f", "ls", "wc"});
    EXPECT_EQ(captured.out, kLsLine + kWcLine);
    EXPECT_EQ(kLsLine, "ls (1)               - list directory contents\n");
    EXPECT_EQ(kWcLine, "wc (1)               - print newline, word, and byte counts for each file\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The name shown is the keyword as typed.
    captured = RunCaptured("man", {"-f", "LS"});
    EXPECT_EQ(captured.out, WhatisLine("LS", "list directory contents"));
    EXPECT_EQ(captured.status, 0);

    // One miss among hits still exits 0.
    captured = RunCaptured("man", {"-f", "ls", "nosuch"});
    EXPECT_EQ(captured.out, kLsLine);
    EXPECT_EQ(captured.err, "nosuch: nothing appropriate.\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("man", {"-f", "nosuch"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "nosuch: nothing appropriate.\n");
    EXPECT_EQ(captured.status, 16);

    captured = RunCaptured("man", {"-f"});
    EXPECT_EQ(captured.out, "whatis what?\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, ManApropos) {
    Captured captured = RunCaptured("man", {"-k", "list director"});
    EXPECT_EQ(captured.out, kLsLine);
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Each matching builtin once, in name order.
    captured = RunCaptured("man", {"-k", "^(ls|cat)$"});
    EXPECT_EQ(captured.out, kCatLine + kLsLine);
    EXPECT_EQ(kCatLine, "cat (1)              - concatenate files and print on the standard output\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("man", {"-k", "nosuchxyz"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "nosuchxyz: nothing appropriate.\n");
    EXPECT_EQ(captured.status, 16);

    // A keyword that is no valid POSIX extended regex is fatal.
    captured = RunCaptured("man", {"-k", "["});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "apropos: fatal: regex `[': Invalid regular expression\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("man", {"-k"});
    EXPECT_EQ(captured.out, "apropos what?\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);

    // Always case-insensitive.
    EXPECT_EQ(RunCaptured("man", {"-k", "LIST"}).out, kLsLine);
}

TEST_F(BuiltinCommandsTest, ManUsageErrors) {
    Captured captured = RunCaptured("man", {"-y"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
        "man: invalid option -- 'y'\n"
        "Try 'man --help' or 'man --usage' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("man", {"--frob"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
        "man: unrecognized option '--frob'\n"
        "Try 'man --help' or 'man --usage' for more information.\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, ManShortHelpAndVersion) {
    // man-db's -? and -V go through the same handling as --help/--version.
    const Captured shortHelp = RunCaptured("man", {"-?"});
    EXPECT_EQ(shortHelp.out, RunCaptured("man", {"--help"}).out);
    EXPECT_EQ(shortHelp.err, "");
    EXPECT_EQ(shortHelp.status, 0);

    const Captured shortVersion = RunCaptured("man", {"-V"});
    EXPECT_EQ(shortVersion.out, "man (HaisosOS builtin) 1.0.0\n");
    EXPECT_EQ(shortVersion.err, "");
    EXPECT_EQ(shortVersion.status, 0);
}

TEST_F(BuiltinCommandsTest, ManNotTreatedOptionsStillShowThePage) {
    const std::string lsHelp = RunCaptured("ls", {"--help"}).out;
    for (const auto& args : {std::vector<std::string>{"-P", "less", "ls"},
                             {"-Tutf8", "ls"}, {"-X100", "ls"}, {"--warnings=all", "ls"}}) {
        const Captured captured = RunCaptured("man", args);
        EXPECT_EQ(captured.out, lsHelp) << args[0];
        const std::string spelling = args[0].rfind("--", 0) == 0
            ? args[0].substr(0, args[0].find('=')) : args[0].substr(0, 2);
        EXPECT_EQ(captured.err, "Parameter " + spelling + " is not treated by HaisosOS man v. 1.0.0\n") << args[0];
        EXPECT_EQ(captured.status, 0) << args[0];
    }
}
