#include <gtest/gtest.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "src/components/Filesystem/BuiltinCommandFile.h"

namespace Haisos {
namespace {

// A counts line as wc prints it with stdin among the inputs (columns 7 wide):
// lines, words, chars, bytes, maximum line width.
std::string StdinWideLine(uint64_t lines, uint64_t words, uint64_t chars, uint64_t bytes, uint64_t line) {
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%7llu %7llu %7llu %7llu %7llu\n",
        static_cast<unsigned long long>(lines), static_cast<unsigned long long>(words),
        static_cast<unsigned long long>(chars), static_cast<unsigned long long>(bytes),
        static_cast<unsigned long long>(line));
    return buffer;
}

} // namespace

TEST_F(BuiltinCommandsTest, WcDefaultCountsOneFile) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    const auto captured = RunCaptured("wc", {"abc.txt"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, " 3  3 14 abc.txt\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, WcSeveralFilesPrintATotal) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    WriteFile("/w/h.txt", "hello world");
    const auto captured = RunCaptured("wc", {"abc.txt", "h.txt"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, " 3  3 14 abc.txt\n 0  2 11 h.txt\n 3  5 25 total\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, WcSingleCountOfOneFileIsUnpadded) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    EXPECT_EQ(RunCaptured("wc", {"-l", "abc.txt"}, std::nullopt, "/w").out, "3 abc.txt\n");
    EXPECT_EQ(RunCaptured("wc", {"-c", "abc.txt"}, std::nullopt, "/w").out, "14 abc.txt\n");
}

TEST_F(BuiltinCommandsTest, WcStdinFromAPipeIsSevenWide) {
    // Standard input has no file size here, so with it the columns are at
    // least 7 wide (GNU would size a redirected file).
    EXPECT_EQ(RunCaptured("wc", {}, "one\ntwo\nthree\n").out, "      3       3      14\n");
    EXPECT_EQ(RunCaptured("wc", {"-l"}, "one\ntwo\nthree\n").out, "3\n");
    EXPECT_EQ(RunCaptured("wc", {"-lw"}, "one\ntwo\nthree\n").out, "      3       3\n");
}

TEST_F(BuiltinCommandsTest, WcDashAmongFiles) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    const auto captured = RunCaptured("wc", {"-", "abc.txt"}, "one\ntwo\nthree\n", "/w");
    EXPECT_EQ(captured.out,
        "      3       3      14 -\n"
        "      3       3      14 abc.txt\n"
        "      6       6      28 total\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, WcTotalAlwaysOnStdin) {
    const auto captured = RunCaptured("wc", {"--total=always"}, "one\ntwo\nthree\n");
    EXPECT_EQ(captured.out, "      3       3      14\n      3       3      14 total\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, WcMissingFileIsReportedAndOthersStillCounted) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    auto captured = RunCaptured("wc", {"nope"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "wc: nope: No such file or directory\n");
    EXPECT_EQ(captured.status, 1);
    // A failed Stat is skipped for the width, so abc.txt's size decides it (2).
    captured = RunCaptured("wc", {"nope", "abc.txt"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, " 3  3 14 abc.txt\n 3  3 14 total\n");
    EXPECT_EQ(captured.err, "wc: nope: No such file or directory\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, WcDirectoryPrintsZerosAndAnError) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    ASSERT_EQ(root->CreateDirectory("/w/d", kDirMode), 0);
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    auto captured = RunCaptured("wc", {"d"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, "      0       0       0 d\n");
    EXPECT_EQ(captured.err, "wc: d: Is a directory\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("wc", {"-l", "d"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, "0 d\n");
    EXPECT_EQ(captured.err, "wc: d: Is a directory\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("wc", {"d", "abc.txt"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out,
        "      0       0       0 d\n"
        "      3       3      14 abc.txt\n"
        "      3       3      14 total\n");
    EXPECT_EQ(captured.err, "wc: d: Is a directory\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, WcNameWithASpaceIsQuotedOnlyInErrors) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/a b", "x\n");
    // In the counts line a name is as given; errors quote only when needed.
    auto captured = RunCaptured("wc", {"a b", "nope2"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, "1 1 2 a b\n1 1 2 total\n");
    EXPECT_EQ(captured.err, "wc: nope2: No such file or directory\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("wc", {"it's"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "wc: \"it's\": No such file or directory\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, WcNameWithANewlineIsQuoted) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/q\nr", "x\n");
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    const auto captured = RunCaptured("wc", {"q\nr", "abc.txt"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, " 1  1  2 'q'$'\\n''r'\n 3  3 14 abc.txt\n 4  4 16 total\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, WcZeroLengthName) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    const auto captured = RunCaptured("wc", {""}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "wc: invalid zero-length file name\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, WcTotalModes) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    WriteFile("/w/h.txt", "hello world");
    EXPECT_EQ(RunCaptured("wc", {"--total=always", "abc.txt", "h.txt"}, std::nullopt, "/w").out,
        " 3  3 14 abc.txt\n 0  2 11 h.txt\n 3  5 25 total\n");
    EXPECT_EQ(RunCaptured("wc", {"--total=only", "abc.txt", "h.txt"}, std::nullopt, "/w").out,
        "3 5 25\n");
    EXPECT_EQ(RunCaptured("wc", {"--total=never", "abc.txt", "h.txt"}, std::nullopt, "/w").out,
        " 3  3 14 abc.txt\n 0  2 11 h.txt\n");
    EXPECT_EQ(RunCaptured("wc", {"-l", "--total=only", "abc.txt", "h.txt"}, std::nullopt, "/w").out,
        "3\n");
    // An unambiguous prefix of a value names it, as GNU's argmatch.
    EXPECT_EQ(RunCaptured("wc", {"--total=al", "abc.txt", "h.txt"}, std::nullopt, "/w").out,
        " 3  3 14 abc.txt\n 0  2 11 h.txt\n 3  5 25 total\n");
    EXPECT_EQ(RunCaptured("wc", {"--tot=o", "abc.txt", "h.txt"}, std::nullopt, "/w").out,
        "3 5 25\n");
}

TEST_F(BuiltinCommandsTest, WcTotalBadValue) {
    const std::string valid =
        "Valid arguments are:\n  - 'auto'\n  - 'always'\n  - 'only'\n  - 'never'\n"
        "Try 'wc --help' for more information.\n";
    auto captured = RunCaptured("wc", {"--total=x"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "wc: invalid argument 'x' for '--total'\n" + valid);
    EXPECT_EQ(captured.status, 1);
    // A prefix of two values is ambiguous rather than invalid; so is the
    // empty value.
    captured = RunCaptured("wc", {"--total=a"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "wc: ambiguous argument 'a' for '--total'\n" + valid);
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("wc", {"--total="});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "wc: ambiguous argument '' for '--total'\n" + valid);
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, WcOrderOfCountsIsFixed) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    // Lines, words, chars, bytes, maximum line width, however they were asked.
    EXPECT_EQ(RunCaptured("wc", {"-clmwL", "abc.txt"}, std::nullopt, "/w").out,
        " 3  3 14 14  5 abc.txt\n");
    EXPECT_EQ(RunCaptured("wc", {"-Lc", "abc.txt"}, std::nullopt, "/w").out,
        "14  5 abc.txt\n");
}

TEST_F(BuiltinCommandsTest, WcMaxLineLength) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    WriteFile("/w/h.txt", "hello world");
    EXPECT_EQ(RunCaptured("wc", {"-L", "abc.txt", "h.txt"}, std::nullopt, "/w").out,
        " 5 abc.txt\n11 h.txt\n11 total\n");
    // A tab moves the position to the next multiple of 8. One count of one
    // input -- stdin included -- is unpadded.
    EXPECT_EQ(RunCaptured("wc", {"-L"}, "abcdefgh\tx\n").out, "17\n");
    // CR ends the line at width 3; the newline's reset does not widen it.
    EXPECT_EQ(RunCaptured("wc", {"-L"}, "abc\rde\n").out, "3\n");
}

TEST_F(BuiltinCommandsTest, WcCountsUtf8) {
    // Every row checked against GNU wc 9.4: printf input -> `wc -lwmcL`.
    struct Row { std::string input; uint64_t lines, words, chars, bytes, width; };
    const std::vector<Row> rows = {
        {"hello world\n", 1, 2, 12, 12, 11},
        {"a\tb\n", 1, 2, 4, 4, 9},
        {"abcdefgh\tx\n", 1, 2, 11, 11, 17},
        {"abc\rde\n", 1, 2, 7, 7, 3},
        {"ab\vcd", 0, 2, 5, 5, 4},
        {std::string("a\x01""b"), 0, 1, 3, 3, 2},
        {std::string("\x01\x02"), 0, 0, 2, 2, 0},
        {std::string("a\0b", 3), 0, 1, 3, 3, 2},   // NUL counts as a character
        {"caf\xC3\xA9\n", 1, 1, 5, 6, 4},          // café: é is one character
        {"e\xCC\x81" "x\n", 1, 1, 4, 5, 2},        // U+0301 combining: width 0
        {"\xE4\xB8\xAD\xE6\x96\x87\n", 1, 1, 3, 7, 4},  // 中文: width 2 each
        {"\xF0\x9F\x98\x80\n", 1, 1, 2, 5, 2},     // U+1F600: width 2
        {"a\xFF" "b\n", 1, 1, 3, 4, 2},            // an invalid byte counts as a byte only
        {"ab\xE2\x82", 0, 1, 2, 4, 2},             // a sequence cut short: bytes only
        {"a\xC2\xA0" "b", 0, 2, 3, 4, 3},          // U+00A0: a word separator
        {"a\xE2\x81\xA0" "b", 0, 2, 3, 5, 2},      // U+2060 word joiner: separator, width 0
        {"a\xE3\x80\x80" "b", 0, 2, 3, 5, 4},      // U+3000: separator, width 2
        {"a\xC2\x85" "b", 0, 1, 3, 4, 2},          // U+0085: not printable, not a separator
        {"a\xC0\x80" "b", 0, 1, 2, 4, 2},          // overlong é: two skipped bytes
    };
    for (const auto& row : rows) {
        EXPECT_EQ(RunCaptured("wc", {"-lwmcL"}, row.input).out,
            StdinWideLine(row.lines, row.words, row.chars, row.bytes, row.width))
            << "input: " << ::testing::PrintToString(row.input);
    }
}

TEST_F(BuiltinCommandsTest, WcCharacterSplitAcrossReadChunksCountsOnce) {
    // wc reads 16 KiB at a time: 16383 'a's put the 3-byte 中 across the
    // first chunk's end, so its first two bytes are carried into the next.
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/big.txt", std::string(16383, 'a') + "\xE4\xB8\xAD\n");
    const auto captured = RunCaptured("wc", {"-lwmcL", "big.txt"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, "    1     1 16385 16387 16385 big.txt\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, WcPosixlyCorrectKeepsNoBreakSpaceInWords) {
    const std::string nbsp("a\xC2\xA0" "b");
    EXPECT_EQ(RunCaptured("wc", {"-w"}, nbsp).out, "2\n");
    auto environment = os->GetOsEnvironment()->Clone();
    environment->SetVariable("POSIXLY_CORRECT", "1");
    EXPECT_EQ(RunCaptured("wc", {"-w"}, nbsp, "/", environment).out, "1\n");
}

TEST_F(BuiltinCommandsTest, WcFilesFromAFile) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    ASSERT_EQ(root->CreateDirectory("/w/d", kDirMode), 0);
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    WriteFile("/w/h.txt", "hello world");
    WriteFile("/w/l0", std::string("abc.txt\0h.txt\0", 14));
    WriteFile("/w/l1", std::string("abc.txt\0\0h.txt", 15));
    WriteFile("/w/l2", std::string("abc.txt"));          // no trailing NUL
    WriteFile("/w/l3", std::string(""));                 // empty: no names at all
    WriteFile("/w/l4", std::string("abc.txt\0nope\0d\0", 15));

    auto captured = RunCaptured("wc", {"--files0-from=l0"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, " 3  3 14 abc.txt\n 0  2 11 h.txt\n 3  5 25 total\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("wc", {"--files0-from=l1"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, " 3  3 14 abc.txt\n 0  2 11 h.txt\n 3  5 25 total\n");
    EXPECT_EQ(captured.err, "wc: l1:2: invalid zero-length file name\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("wc", {"--files0-from=l2"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, " 3  3 14 abc.txt\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("wc", {"--files0-from=l3"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A directory among the names sets the 7-wide minimum and adds its zeros.
    captured = RunCaptured("wc", {"--files0-from=l4"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out,
        "      3       3      14 abc.txt\n"
        "      0       0       0 d\n"
        "      3       3      14 total\n");
    EXPECT_EQ(captured.err, "wc: nope: No such file or directory\nwc: d: Is a directory\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, WcFilesFromStdin) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    WriteFile("/w/h.txt", "hello world");
    // Names as a stream: width 1, no sizes looked at first.
    auto captured = RunCaptured("wc", {"--files0-from=-"},
        std::string("abc.txt\0h.txt\0", 14), "/w");
    EXPECT_EQ(captured.out, "3 3 14 abc.txt\n0 2 11 h.txt\n3 5 25 total\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    // Reading names from stdin, a '-' name would read it again: refused.
    captured = RunCaptured("wc", {"--files0-from=-"},
        std::string("-\0abc.txt\0", 10), "/w");
    EXPECT_EQ(captured.out, "3 3 14 abc.txt\n3 3 14 total\n");
    EXPECT_EQ(captured.err, "wc: when reading file names from stdin, no file name of '-' allowed\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, WcFilesFromErrors) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    ASSERT_EQ(root->CreateDirectory("/w/d", kDirMode), 0);
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    auto captured = RunCaptured("wc", {"--files0-from=nofile"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "wc: cannot open 'nofile' for reading: No such file or directory\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("wc", {"--files0-from=abc.txt", "x"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
        "wc: extra operand 'x'\n"
        "file operands cannot be combined with --files0-from\n"
        "Try 'wc --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("wc", {"--files0-from=d"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "wc: d: read error: Is a directory\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, WcUsageErrors) {
    auto captured = RunCaptured("wc", {"-x"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "wc: invalid option -- 'x'\nTry 'wc --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("wc", {"--frob"});
    EXPECT_EQ(captured.err, "wc: unrecognized option '--frob'\nTry 'wc --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("wc", {"--files0-from"});
    EXPECT_EQ(captured.err, "wc: option '--files0-from' requires an argument\nTry 'wc --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, WcDebugIsNotTreated) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/abc.txt", "one\ntwo\nthree\n");
    const auto captured = RunCaptured("wc", {"--debug", "abc.txt"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, " 3  3 14 abc.txt\n");
    EXPECT_EQ(captured.err, "Parameter --debug is not treated by HaisosOS wc v. 1.0.0\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, WcReadsTheBuiltinNoteOfAPath) {
    const auto captured = RunCaptured("wc", {"-c", "/bin/ls"});
    EXPECT_EQ(captured.out, std::to_string(BuiltinCommandFileContent("ls").size()) + " /bin/ls\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

} // namespace Haisos
