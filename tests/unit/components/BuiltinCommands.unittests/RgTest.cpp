#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"

using namespace Haisos;

namespace {

// grep-core's a.c: 64 bytes, three TODO lines (2, 5 and 10).
const std::string kAc =
    "one\ntwo TODO\nthree\nfour\nfive TODO\nsix\nseven\neight\nnine\nten TODO\n";

void WriteTo(const std::shared_ptr<IFileSystem>& fs, const std::string& path, const std::string& content) {
    auto file = fs->OpenFile(path, kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(file, nullptr) << path;
    file->Write(content.data(), content.size());
}

// /r with the plan's tree: src/a.c, src/sub/b.h, src/bin.dat (binary), t1 and
// k (no TODO). The pattern files pf and empty are not part of it: a search of
// . would print pf's TODO line, and only RgPatterns uses them -- it writes
// them itself.
void MakeRgTree(const std::shared_ptr<IFileSystem>& fs) {
    ASSERT_EQ(fs->CreateDirectory("/r", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/r/src", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/r/src/sub", kDirMode), 0);
    WriteTo(fs, "/r/src/a.c", kAc);
    WriteTo(fs, "/r/src/sub/b.h", "TODO sub\n");
    WriteTo(fs, "/r/src/bin.dat", std::string("x\0y TODO\n", 9));
    WriteTo(fs, "/r/t1", "TODO\n");
    WriteTo(fs, "/r/k", "none\n");
}

// rg's colours, byte for byte, for building the expected strings.
std::string CPath(const std::string& text) {
    return "\x1b[0m\x1b[35m" + text + "\x1b[0m";
}
std::string CLineNumber(const std::string& text) {
    return "\x1b[0m\x1b[32m" + text + "\x1b[0m";
}
std::string CMatch(const std::string& text) {
    return "\x1b[0m\x1b[1m\x1b[31m" + text + "\x1b[0m";
}

} // namespace

// --- rg: what is searched, and the two output styles ---

TEST_F(BuiltinCommandsTest, RgSearchesDotByDefault) {
    MakeRgTree(root);
    // No stdin input: an input empty at once means no standard input, so the
    // implicit path is searched, without ./ prefixes.
    const Captured captured = RunCaptured("rg", {"TODO"}, std::nullopt, "/r");
    EXPECT_EQ(captured.out,
        "src/a.c:two TODO\nsrc/a.c:five TODO\nsrc/a.c:ten TODO\nsrc/sub/b.h:TODO sub\nt1:TODO\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, RgExplicitDotKeepsPrefix) {
    MakeRgTree(root);
    const Captured captured = RunCaptured("rg", {"TODO", "."}, std::nullopt, "/r");
    EXPECT_EQ(captured.out,
        "./src/a.c:two TODO\n./src/a.c:five TODO\n./src/a.c:ten TODO\n"
        "./src/sub/b.h:TODO sub\n./t1:TODO\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, RgSingleFile) {
    MakeRgTree(root);
    // One file operand: no path prefix, and line numbers only when asked.
    EXPECT_EQ(RunCaptured("rg", {"TODO", "t1"}, std::nullopt, "/r").out, "TODO\n");
    EXPECT_EQ(RunCaptured("rg", {"-n", "TODO", "t1"}, std::nullopt, "/r").out, "1:TODO\n");
    EXPECT_EQ(RunCaptured("rg", {"-H", "TODO", "t1"}, std::nullopt, "/r").out, "t1:TODO\n");
}

TEST_F(BuiltinCommandsTest, RgTerminalStyle) {
    MakeRgTree(root);
    // On a terminal: a heading per file, line numbers, a blank line between
    // files; the binary file met while walking ends silently.
    int status = -1;
    const Lines lines = Run("rg", {"TODO", "src"}, &status, "/r");
    EXPECT_EQ(status, 0);
    EXPECT_EQ(lines, (Lines{"src/a.c", "2:two TODO", "5:five TODO", "10:ten TODO", "",
                            "src/sub/b.h", "1:TODO sub"}));
}

// --- rg: context ---

TEST_F(BuiltinCommandsTest, RgContext) {
    MakeRgTree(root);
    const Captured both = RunCaptured("rg", {"-n", "-A1", "-B1", "five|ten", "src/a.c"}, std::nullopt, "/r");
    EXPECT_EQ(both.out, "4-four\n5:five TODO\n6-six\n--\n9-nine\n10:ten TODO\n");
    EXPECT_EQ(both.status, 0);
    // Off a terminal, files keep their prefixes and the groups of different
    // files are separated too.
    const Captured across = RunCaptured(
        "rg", {"-A1", "TODO", "src/sub/b.h", "t1", "src/a.c"}, std::nullopt, "/r");
    EXPECT_EQ(across.out,
        "src/sub/b.h:TODO sub\n--\nt1:TODO\n--\nsrc/a.c:two TODO\nsrc/a.c-three\n--\n"
        "src/a.c:five TODO\nsrc/a.c-six\n--\nsrc/a.c:ten TODO\n");
    EXPECT_EQ(across.status, 0);
    // With --heading the path stands above the lines and no separator goes
    // between files.
    const Captured heading = RunCaptured(
        "rg", {"--heading", "-n", "-A1", "TODO", "src/a.c", "t1"}, std::nullopt, "/r");
    EXPECT_EQ(heading.out,
        "src/a.c\n2:two TODO\n3-three\n--\n5:five TODO\n6-six\n--\n10:ten TODO\n\n"
        "t1\n1:TODO\n");
    // -m passes later matches inside the trailing context as matches.
    const Captured maxed = RunCaptured("rg", {"-n", "-m1", "-A4", "TODO", "src/a.c"}, std::nullopt, "/r");
    EXPECT_EQ(maxed.out, "2:two TODO\n3-three\n4-four\n5:five TODO\n6-six\n");
    EXPECT_EQ(maxed.status, 0);
}

// --- rg: the output modes ---

TEST_F(BuiltinCommandsTest, RgCountsAndLists) {
    MakeRgTree(root);
    EXPECT_EQ(RunCaptured("rg", {"-c", "TODO", "src"}, std::nullopt, "/r").out,
        "src/a.c:3\nsrc/sub/b.h:1\n");
    EXPECT_EQ(RunCaptured("rg", {"-c", "TODO", "t1", "k"}, std::nullopt, "/r").out, "t1:1\n");
    EXPECT_EQ(RunCaptured("rg", {"--include-zero", "-c", "TODO", "t1", "k"}, std::nullopt, "/r").out,
        "t1:1\nk:0\n");
    EXPECT_EQ(RunCaptured("rg", {"--count-matches", "T", "src/a.c"}, std::nullopt, "/r").out, "3\n");
    const Captured listed = RunCaptured("rg", {"-l", "TODO"}, std::nullopt, "/r");
    EXPECT_EQ(listed.out, "src/a.c\nsrc/sub/b.h\nt1\n");
    EXPECT_EQ(listed.status, 0);
    const Captured without = RunCaptured("rg", {"--files-without-match", "TODO", "t1", "k"}, std::nullopt, "/r");
    EXPECT_EQ(without.out, "k\n");
    EXPECT_EQ(without.status, 0);
    const Captured noneWithout = RunCaptured("rg", {"--files-without-match", "TODO", "t1"}, std::nullopt, "/r");
    EXPECT_EQ(noneWithout.out, "");
    EXPECT_EQ(noneWithout.status, 1);
    const Captured noneListed = RunCaptured("rg", {"-l", "TODO", "k"}, std::nullopt, "/r");
    EXPECT_EQ(noneListed.out, "");
    EXPECT_EQ(noneListed.status, 1);
}

TEST_F(BuiltinCommandsTest, RgOnlyMatchingAndColumns) {
    MakeRgTree(root);
    EXPECT_EQ(RunCaptured("rg", {"-o", "-n", "T.D", "src/a.c"}, std::nullopt, "/r").out,
        "2:TOD\n5:TOD\n10:TOD\n");
    EXPECT_EQ(RunCaptured("rg", {"-b", "TODO", "t1", "src/a.c"}, std::nullopt, "/r").out,
        "t1:0:TODO\nsrc/a.c:4:two TODO\nsrc/a.c:24:five TODO\nsrc/a.c:55:ten TODO\n");
    EXPECT_EQ(RunCaptured("rg", {"--column", "-n", "TODO", "t1"}, std::nullopt, "/r").out,
        "1:1:TODO\n");
}

TEST_F(BuiltinCommandsTest, RgNull) {
    MakeRgTree(root);
    const Captured listed = RunCaptured("rg", {"-0", "-l", "TODO", "src"}, std::nullopt, "/r");
    EXPECT_EQ(listed.out, std::string("src/a.c\0", 8) + std::string("src/sub/b.h\0", 12));
    EXPECT_EQ(listed.status, 0);
    const Captured lines = RunCaptured(
        "rg", {"-0", "-n", "TODO", "src/sub/b.h", "src/a.c"}, std::nullopt, "/r");
    EXPECT_EQ(lines.out, std::string("src/sub/b.h\0", 12) + "1:TODO sub\n"
        + std::string("src/a.c\0", 8) + "2:two TODO\n"
        + std::string("src/a.c\0", 8) + "5:five TODO\n"
        + std::string("src/a.c\0", 8) + "10:ten TODO\n");
}

TEST_F(BuiltinCommandsTest, RgMaxColumns) {
    MakeRgTree(root);
    const Captured matches = RunCaptured(
        "rg", {"-M", "5", "-n", "TODO", "src/a.c", "src/sub/b.h"}, std::nullopt, "/r");
    EXPECT_EQ(matches.out,
        "src/a.c:2:[Omitted long matching line]\nsrc/a.c:5:[Omitted long matching line]\n"
        "src/a.c:10:[Omitted long matching line]\nsrc/sub/b.h:1:[Omitted long matching line]\n");
    EXPECT_EQ(matches.status, 0);
    const Captured context = RunCaptured("rg", {"-M", "3", "-n", "-A1", "two", "src/a.c"}, std::nullopt, "/r");
    EXPECT_EQ(context.out, "2:[Omitted long matching line]\n3-[Omitted long context line]\n");
}

// --- rg: matching ---

TEST_F(BuiltinCommandsTest, RgCaseFlags) {
    MakeRgTree(root);
    EXPECT_EQ(RunCaptured("rg", {"-S", "todo", "t1"}, std::nullopt, "/r").out, "TODO\n");
    EXPECT_EQ(RunCaptured("rg", {"-S", "Todo", "t1"}, std::nullopt, "/r").status, 1);
    // The last of -s/-i/-S wins.
    EXPECT_EQ(RunCaptured("rg", {"-i", "-s", "todo", "t1"}, std::nullopt, "/r").status, 1);
    EXPECT_EQ(RunCaptured("rg", {"-s", "-i", "todo", "t1"}, std::nullopt, "/r").out, "TODO\n");
    EXPECT_EQ(RunCaptured("rg", {"-F", "T.D", "t1"}, std::nullopt, "/r").status, 1);
    EXPECT_EQ(RunCaptured("rg", {"-w", "-o", "f\\w*", "src/a.c"}, std::nullopt, "/r").out,
        "four\nfive\n");
    EXPECT_EQ(RunCaptured("rg", {"-x", "six", "src/a.c"}, std::nullopt, "/r").out, "six\n");
}

TEST_F(BuiltinCommandsTest, RgPatterns) {
    MakeRgTree(root);
    // -e patterns joined leftmost-first: "a" wins over "ab" on "ab".
    const Captured joined = RunCaptured("rg", {"-o", "-e", "a", "-e", "ab"}, "ab\n");
    EXPECT_EQ(joined.out, "a\n");
    EXPECT_EQ(joined.status, 0);
    // pf holds TODO and an empty pattern (its trailing blank line): an empty
    // pattern matches every line, so k counts 1.
    WriteTo(root, "/r/pf", "TODO\n\n");
    WriteTo(root, "/r/empty", "");
    EXPECT_EQ(RunCaptured("rg", {"-c", "-f", "pf", "k"}, std::nullopt, "/r").out, "1\n");
    const Captured noPatterns = RunCaptured("rg", {"-f", "empty", "k"}, std::nullopt, "/r");
    EXPECT_EQ(noPatterns.out, "");
    EXPECT_EQ(noPatterns.status, 1);
    // A pattern holding a newline byte: rg's multiline message.
    const Captured newline = RunCaptured("rg", {"a\nb"}, std::nullopt, "/r");
    EXPECT_EQ(newline.err,
        "rg: the literal \"\\n\" is not allowed in a regex\n"
        "\n"
        "Consider enabling multiline mode with the --multiline flag (or -U for short).\n"
        "When multiline mode is enabled, new line characters can be matched.\n");
    EXPECT_EQ(newline.status, 2);
}

TEST_F(BuiltinCommandsTest, RgStdin) {
    MakeRgTree(root);
    const Captured plain = RunCaptured("rg", {"TODO"}, "a TODO\n", "/r");
    EXPECT_EQ(plain.out, "a TODO\n");
    EXPECT_EQ(plain.status, 0);
    const Captured named = RunCaptured("rg", {"-H", "-n", "TODO"}, "a TODO\n", "/r");
    EXPECT_EQ(named.out, "<stdin>:1:a TODO\n");
    // -m stops at its match, an unterminated last line of the deciding read
    // included.
    const Captured maxed = RunCaptured("rg", {"-m1", "TODO"}, "a TODO\nb TODO", "/r");
    EXPECT_EQ(maxed.out, "a TODO\n");
    EXPECT_EQ(maxed.status, 0);
}

// --- rg: binary files ---

TEST_F(BuiltinCommandsTest, RgBinary) {
    MakeRgTree(root);
    // An operand: the first match ends the file with rg's message on stdout.
    const Captured operand = RunCaptured("rg", {"TODO", "src/bin.dat"}, std::nullopt, "/r");
    EXPECT_EQ(operand.out, "binary file matches (found \"\\0\" byte around offset 1)\n");
    EXPECT_EQ(operand.err, "");
    EXPECT_EQ(operand.status, 0);
    // Met while walking: the file ends silently.
    const Captured walked = RunCaptured("rg", {"TODO", "src"}, std::nullopt, "/r");
    EXPECT_EQ(walked.out,
        "src/a.c:two TODO\nsrc/a.c:five TODO\nsrc/a.c:ten TODO\nsrc/sub/b.h:TODO sub\n");
    EXPECT_EQ(walked.status, 0);
    // The counts keep going: -c counts the matching line of a binary operand.
    EXPECT_EQ(RunCaptured("rg", {"-c", "TODO", "src/bin.dat"}, std::nullopt, "/r").out, "1\n");
    // Standard input is searched as an operand is: its deciding read holds
    // the NUL, and its first match ends it with the message.
    const Captured piped = RunCaptured("rg", {"TODO"}, std::string("x\0y TODO\n", 9), "/r");
    EXPECT_EQ(piped.out, "binary file matches (found \"\\0\" byte around offset 1)\n");
    EXPECT_EQ(piped.status, 0);
    // -a: no binary detection, the line printed raw.
    EXPECT_EQ(RunCaptured("rg", {"-a", "TODO", "src/bin.dat"}, std::nullopt, "/r").out,
        std::string("x\0y TODO\n", 9));
}

// --- rg: diagnostics ---

TEST_F(BuiltinCommandsTest, RgErrors) {
    MakeRgTree(root);
    // A missing path, alone: rg's longer message.
    const Captured alone = RunCaptured("rg", {"x", "nosuch"}, std::nullopt, "/r");
    EXPECT_EQ(alone.err,
        "rg: nosuch: IO error for operation on nosuch: No such file or directory (os error 2)\n");
    EXPECT_EQ(alone.status, 2);
    // Beside another operand: the shorter one.
    const Captured beside = RunCaptured("rg", {"x", "nosuch", "t1"}, std::nullopt, "/r");
    EXPECT_EQ(beside.err, "rg: nosuch: No such file or directory (os error 2)\n");
    EXPECT_EQ(beside.status, 2);
    // -q found its match: the error does not reach the exit status.
    const Captured quiet = RunCaptured("rg", {"-q", "TODO", "nosuch", "t1"}, std::nullopt, "/r");
    EXPECT_EQ(quiet.out, "");
    EXPECT_EQ(quiet.status, 0);
    const Captured noPattern = RunCaptured("rg", {}, std::nullopt, "/r");
    EXPECT_EQ(noPattern.err, "rg: ripgrep requires at least one pattern to execute a search\n");
    EXPECT_EQ(noPattern.status, 2);
    const Captured longFlag = RunCaptured("rg", {"--foo", "x"}, std::nullopt, "/r");
    EXPECT_EQ(longFlag.err, "rg: unrecognized flag --foo\n");
    EXPECT_EQ(longFlag.status, 2);
    const Captured shortFlag = RunCaptured("rg", {"-k", "x"}, std::nullopt, "/r");
    EXPECT_EQ(shortFlag.err, "rg: unrecognized flag -k\n");
    EXPECT_EQ(shortFlag.status, 2);
    const Captured missingValue = RunCaptured("rg", {"-e"}, std::nullopt, "/r");
    EXPECT_EQ(missingValue.err, "rg: missing value for flag -e: missing argument for option '-e'\n");
    EXPECT_EQ(missingValue.status, 2);
    const Captured attached = RunCaptured("rg", {"--heading=yes", "x"}, std::nullopt, "/r");
    EXPECT_EQ(attached.err,
        "rg: invalid CLI arguments: unexpected argument for option '--heading': \"yes\"\n");
    EXPECT_EQ(attached.status, 2);
    const Captured number = RunCaptured("rg", {"-A", "x", "y"}, std::nullopt, "/r");
    EXPECT_EQ(number.err,
        "rg: error parsing flag -A: value is not a valid number: invalid digit found in string\n");
    EXPECT_EQ(number.status, 2);
    const Captured color = RunCaptured("rg", {"--color=bad", "x"}, std::nullopt, "/r");
    EXPECT_EQ(color.err, "rg: error parsing flag --color: choice 'bad' is unrecognized\n");
    EXPECT_EQ(color.status, 2);
}

// --- rg: Rust-regex patterns ---

TEST_F(BuiltinCommandsTest, RgRegexErrorExitsTwo) {
    MakeRgTree(root);
    // A pattern Rust refuses, with ripgrep's frame.
    const Captured captured = RunCaptured("rg", {"(", "t1"}, std::nullopt, "/r");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
        "rg: regex parse error:\n    (?:()\n    ^\nerror: unclosed group\n");
    EXPECT_EQ(captured.status, 2);
    // -S: no uppercase letter a literal spells, so the search is insensitive.
    EXPECT_EQ(RunCaptured("rg", {"-S", "\\btodo", "t1"}, std::nullopt, "/r").out, "TODO\n");
}

TEST_F(BuiltinCommandsTest, RgRegexNewlineEscape) {
    MakeRgTree(root);
    // \n spelled as an escape: rg's multiline message, as a raw newline is.
    const Captured captured = RunCaptured("rg", {"a\\nb", "t1"}, std::nullopt, "/r");
    EXPECT_EQ(captured.err,
        "rg: the literal \"\\n\" is not allowed in a regex\n"
        "\n"
        "Consider enabling multiline mode with the --multiline flag (or -U for short).\n"
        "When multiline mode is enabled, new line characters can be matched.\n");
    EXPECT_EQ(captured.status, 2);
    // a class holding only '\n' is left empty by the strip: the same message
    const Captured onlyNewline = RunCaptured("rg", {"[\\n]", "t1"}, std::nullopt, "/r");
    EXPECT_EQ(onlyNewline.err, captured.err);
    EXPECT_EQ(onlyNewline.status, 2);
    // other members survive the strip: the class matches them
    EXPECT_EQ(RunCaptured("rg", {"[\\nT]", "t1"}, std::nullopt, "/r").out, "TODO\n");
}

TEST_F(BuiltinCommandsTest, RgPatternFileFailures) {
    MakeRgTree(root);
    const Captured directory = RunCaptured("rg", {"-f", "src", "TODO", "t1"}, std::nullopt, "/r");
    EXPECT_EQ(directory.err, "rg: src: Is a directory (os error 21)\n");
    EXPECT_EQ(directory.status, 2);
    const Captured missing = RunCaptured("rg", {"-f", "nosuch", "TODO", "t1"}, std::nullopt, "/r");
    EXPECT_EQ(missing.err, "rg: nosuch: No such file or directory (os error 2)\n");
    EXPECT_EQ(missing.status, 2);
}

TEST_F(BuiltinCommandsTest, RgNoMessagesOnAnEmptyTree) {
    MakeRgTree(root);
    ASSERT_EQ(root->CreateDirectory("/r/empty", kDirMode), 0);
    // Nothing searched at all: rg's message, and --no-messages suppresses
    // it (the exit stays 2).
    const Captured quiet = RunCaptured("rg", {"--no-messages", "TODO"}, std::nullopt, "/r/empty");
    EXPECT_EQ(quiet.out, "");
    EXPECT_EQ(quiet.err, "");
    EXPECT_EQ(quiet.status, 2);
    const Captured loud = RunCaptured("rg", {"TODO"}, std::nullopt, "/r/empty");
    EXPECT_EQ(loud.err,
        "rg: No files were searched, which means ripgrep probably applied a filter you "
        "didn't expect.\nRunning with --debug will show why files are being skipped.\n");
    EXPECT_EQ(loud.status, 2);
}

TEST_F(BuiltinCommandsTest, RgFilesLists) {
    MakeRgTree(root);
    const Captured one = RunCaptured("rg", {"--files", "src"}, std::nullopt, "/r");
    EXPECT_EQ(one.out, "src/a.c\nsrc/bin.dat\nsrc/sub/b.h\n");
    EXPECT_EQ(one.status, 0);
    const Captured two = RunCaptured("rg", {"--files", "src", "t1"}, std::nullopt, "/r");
    EXPECT_EQ(two.out, "src/a.c\nsrc/bin.dat\nsrc/sub/b.h\nt1\n");
    EXPECT_EQ(two.status, 0);
}

// --- rg: colours ---

TEST_F(BuiltinCommandsTest, RgColor) {
    MakeRgTree(root);
    // --color=always: every piece in its own SGR run, byte for byte.
    const Captured lines = RunCaptured(
        "rg", {"--color=always", "-n", "TODO", "src/a.c", "t1"}, std::nullopt, "/r");
    EXPECT_EQ(lines.out,
        CPath("src/a.c") + ":" + CLineNumber("2") + ":two " + CMatch("TODO") + "\n"
        + CPath("src/a.c") + ":" + CLineNumber("5") + ":five " + CMatch("TODO") + "\n"
        + CPath("src/a.c") + ":" + CLineNumber("10") + ":ten " + CMatch("TODO") + "\n"
        + CPath("t1") + ":" + CLineNumber("1") + ":" + CMatch("TODO") + "\n");
    // A single file operand: no path, and the pattern itself is highlighted.
    const Captured context = RunCaptured(
        "rg", {"--color=always", "-n", "-A1", "five", "src/a.c"}, std::nullopt, "/r");
    EXPECT_EQ(context.out,
        CLineNumber("5") + ":" + CMatch("five") + " TODO\n"
        + CLineNumber("6") + "-six\n");
    const Captured counts = RunCaptured(
        "rg", {"--color=always", "-c", "TODO", "src/a.c", "src/sub/b.h"}, std::nullopt, "/r");
    EXPECT_EQ(counts.out, CPath("src/a.c") + ":3\n" + CPath("src/sub/b.h") + ":1\n");
    const Captured files = RunCaptured("rg", {"--color=always", "--files", "src"}, std::nullopt, "/r");
    EXPECT_EQ(files.out, CPath("src/a.c") + "\n" + CPath("src/bin.dat") + "\n" + CPath("src/sub/b.h") + "\n");
    // auto: a terminal with TERM set colours; TERM=dumb does not.
    os->GetOsEnvironment()->SetVariable("TERM", "xterm");
    int status = -1;
    const Lines colored = Run("rg", {"TODO", "src"}, &status, "/r");
    EXPECT_EQ(status, 0);
    ASSERT_GE(colored.size(), 2u);
    EXPECT_EQ(colored[0], CPath("src/a.c"));
    EXPECT_EQ(colored[1], CLineNumber("2") + ":two " + CMatch("TODO"));
    os->GetOsEnvironment()->SetVariable("TERM", "dumb");
    const Lines plain = Run("rg", {"TODO", "src"}, nullptr, "/r");
    ASSERT_GE(plain.size(), 2u);
    EXPECT_EQ(plain[0], "src/a.c");
    EXPECT_EQ(plain[1], "2:two TODO");
}

// --- rg: --help and --version ---

TEST_F(BuiltinCommandsTest, RgHelpAndVersion) {
    const Captured version = RunCaptured("rg", {"--version"});
    EXPECT_EQ(version.out, "rg (HaisosOS builtin) 1.1.0\n");
    EXPECT_EQ(version.status, 0);
    // ripgrep has no man7 page: the reference line links its guide.
    int status = -1;
    const Lines help = Run("rg", {"--help"}, &status);
    EXPECT_EQ(status, 0);
    ASSERT_GE(help.size(), 5u);
    EXPECT_EQ(help[0], "HaisosOS rg version 1.1.0 - recursively search the current directory "
                       "for lines matching a pattern");
    EXPECT_EQ(help[1], "Based on Linux rg: https://github.com/BurntSushi/ripgrep/blob/14.1.1/GUIDE.md");
    EXPECT_EQ(help[3], "Usage: rg [OPTIONS] PATTERN [PATH ...]");
    EXPECT_EQ(help.back().rfind("Not treated arguments: ", 0), 0u);
    // The ignore rules of a later develop, and the rest Haisos lacks.
    EXPECT_NE(help.back().find("-., --hidden"), std::string::npos);
    EXPECT_NE(help.back().find("--pcre2"), std::string::npos);
    const Lines shortHelp = Run("rg", {"-h"}, &status);
    EXPECT_EQ(status, 0);
    EXPECT_EQ(shortHelp, help);
}