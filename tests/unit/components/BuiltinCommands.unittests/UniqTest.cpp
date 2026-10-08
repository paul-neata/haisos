#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "src/components/Filesystem/FilesystemUtils.h"

using namespace Haisos;

namespace {

// The plan's group, byte for byte: no final newline.
const std::string kGroup = "a\na\nb\nc\nc\nc\nd";

const char* kTryUniq = "Try 'uniq --help' for more information.\n";

} // namespace

// --- uniq ---

TEST_F(BuiltinCommandsTest, UniqDefault) {
    const Captured captured = RunCaptured("uniq", {}, kGroup);
    EXPECT_EQ(captured.out, "a\nb\nc\nd\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, UniqCount) {
    const Captured captured = RunCaptured("uniq", {"-c"}, kGroup);
    EXPECT_EQ(captured.out, "      2 a\n      1 b\n      3 c\n      1 d\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    // A count wider than seven columns is not cut short.
    std::string tenA;
    for (int i = 0; i < 10; ++i) {
        tenA += "a\n";
    }
    const Captured ten = RunCaptured("uniq", {"-c"}, tenA);
    EXPECT_EQ(ten.out, "     10 a\n");
    EXPECT_EQ(ten.status, 0);
}

TEST_F(BuiltinCommandsTest, UniqRepeatedAndUnique) {
    EXPECT_EQ(RunCaptured("uniq", {"-d"}, kGroup).out, "a\nc\n");
    EXPECT_EQ(RunCaptured("uniq", {"-D"}, kGroup).out, "a\na\nc\nc\nc\n");
    EXPECT_EQ(RunCaptured("uniq", {"-u"}, kGroup).out, "b\nd\n");
    EXPECT_EQ(RunCaptured("uniq", {"-cd"}, kGroup).out, "      2 a\n      3 c\n");
    EXPECT_EQ(RunCaptured("uniq", {"-du"}, kGroup).out, "");
    // With -D and -u together, only the later lines of a group print.
    const Captured both = RunCaptured("uniq", {"-D", "-u"}, "a\na\nb\nb\nc\n");
    EXPECT_EQ(both.out, "a\nb\n");
    EXPECT_EQ(both.status, 0);
}

TEST_F(BuiltinCommandsTest, UniqAllRepeatedMethods) {
    const Captured separate = RunCaptured("uniq", {"--all-repeated=separate"}, kGroup);
    EXPECT_EQ(separate.out, "a\na\n\nc\nc\nc\n");
    EXPECT_EQ(separate.status, 0);
    const Captured prepend = RunCaptured("uniq", {"--all-repeated=prepend"}, kGroup);
    EXPECT_EQ(prepend.out, "\na\na\n\nc\nc\nc\n");
    EXPECT_EQ(prepend.status, 0);
    // An unambiguous prefix of a method name, as getopt_long allows.
    const Captured prefix = RunCaptured("uniq", {"--all-repeated=se"}, kGroup);
    EXPECT_EQ(prefix.out, separate.out);
    EXPECT_EQ(prefix.status, 0);
}

TEST_F(BuiltinCommandsTest, UniqGroupMethods) {
    const Captured separate = RunCaptured("uniq", {"--group"}, kGroup);
    EXPECT_EQ(separate.out, "a\na\n\nb\n\nc\nc\nc\n\nd\n");
    EXPECT_EQ(separate.status, 0);
    const Captured prepend = RunCaptured("uniq", {"--group=prepend"}, kGroup);
    EXPECT_EQ(prepend.out, "\na\na\n\nb\n\nc\nc\nc\n\nd\n");
    EXPECT_EQ(prepend.status, 0);
    const Captured append = RunCaptured("uniq", {"--group=append"}, kGroup);
    EXPECT_EQ(append.out, "a\na\n\nb\n\nc\nc\nc\n\nd\n\n");
    EXPECT_EQ(append.status, 0);
    const Captured both = RunCaptured("uniq", {"--group=both"}, kGroup);
    EXPECT_EQ(both.out, "\na\na\n\nb\n\nc\nc\nc\n\nd\n\n");
    EXPECT_EQ(both.status, 0);
}

TEST_F(BuiltinCommandsTest, UniqSkipAndCheck) {
    const Captured fields = RunCaptured("uniq", {"-f1", "-c"}, "x a\ny a\nz b\n");
    EXPECT_EQ(fields.out, "      2 x a\n      1 z b\n");
    EXPECT_EQ(fields.status, 0);
    EXPECT_EQ(RunCaptured("uniq", {"-s1"}, "xa\nya\nzb\n").out, "xa\nzb\n");
    EXPECT_EQ(RunCaptured("uniq", {"-w2"}, "ab1\nab2\nac\n").out, "ab1\nac\n");
    EXPECT_EQ(RunCaptured("uniq", {"-i"}, "A\na\nb\n").out, "A\nb\n");
}

TEST_F(BuiltinCommandsTest, UniqObsoleteForms) {
    const std::string longLines =
        "a b c d e f g h i j k l 1\nx y z w v u t s r q p o 1\n";
    // -12 is the two short options 1 and 2: twelve fields skipped, so both
    // lines compare as their last field, "1".
    const Captured clustered = RunCaptured("uniq", {"-12"}, longLines);
    EXPECT_EQ(clustered.out, "a b c d e f g h i j k l 1\n");
    EXPECT_EQ(clustered.status, 0);
    // The digits glue together the same way.
    const Captured digits = RunCaptured("uniq", {"-1", "-2"}, longLines);
    EXPECT_EQ(digits.out, clustered.out);
    EXPECT_EQ(digits.status, 0);
    // An -f since the last digit starts the count over: two fields.
    const Captured reset = RunCaptured("uniq", {"-1", "-f", "0", "-2"}, longLines);
    EXPECT_EQ(reset.out, longLines);
    EXPECT_EQ(reset.status, 0);
    // +N, an obsolete operand: one byte skipped.
    const Captured plus = RunCaptured("uniq", {"+1"}, "ab\ncb\n");
    EXPECT_EQ(plus.out, "ab\n");
    EXPECT_EQ(plus.status, 0);
}

TEST_F(BuiltinCommandsTest, UniqZeroTerminated) {
    const Captured captured = RunCaptured("uniq", {"-z"}, std::string("a\0a\0b", 5));
    EXPECT_EQ(captured.out, std::string("a\0b\0", 4));
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, UniqOutputFile) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/in", kGroup);
    const Captured captured = RunCaptured("uniq", {"in", "out"}, std::nullopt, "/w");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/w/out", content));
    EXPECT_EQ(content, "a\nb\nc\nd\n");
}

TEST_F(BuiltinCommandsTest, UniqHelpHidesTheObsoleteDigits) {
    const Captured captured = RunCaptured("uniq", {"--help"});
    EXPECT_EQ(captured.status, 0);
    // -0 .. -9 are parsed, never documented.
    EXPECT_EQ(captured.out.find("-0"), std::string::npos);
    EXPECT_EQ(captured.out.find("-9"), std::string::npos);
}

TEST_F(BuiltinCommandsTest, UniqErrors) {
    Captured captured = RunCaptured("uniq", {"--group", "-c"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "uniq: --group is mutually exclusive with -c/-d/-D/-u\n" + std::string(kTryUniq));
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("uniq", {"-D", "-c"}, "");
    EXPECT_EQ(captured.err, "uniq: printing all duplicated lines and repeat counts is meaningless\n" + std::string(kTryUniq));
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("uniq", {"--all-repeated=foo"}, "");
    EXPECT_EQ(captured.err,
        "uniq: invalid argument 'foo' for '--all-repeated'\n"
        "Valid arguments are:\n"
        "  - 'none'\n"
        "  - 'prepend'\n"
        "  - 'separate'\n" + std::string(kTryUniq));
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("uniq", {"-f", "x"}, "");
    EXPECT_EQ(captured.err, "uniq: x: invalid number of fields to skip\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("uniq", {"-s", "-1"}, "");
    EXPECT_EQ(captured.err, "uniq: -1: invalid number of bytes to skip\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("uniq", {"-w", "x"}, "");
    EXPECT_EQ(captured.err, "uniq: x: invalid number of bytes to compare\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("uniq", {"a", "b", "c"}, "");
    EXPECT_EQ(captured.err, "uniq: extra operand 'c'\n" + std::string(kTryUniq));
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("uniq", {"nofile"}, "");
    EXPECT_EQ(captured.err, "uniq: nofile: No such file or directory\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("uniq", {"/docs"}, "");
    EXPECT_EQ(captured.err, "uniq: error reading '/docs': Is a directory\n");
    EXPECT_EQ(captured.status, 1);

    WriteFile("/in", kGroup);
    captured = RunCaptured("uniq", {"in", "/nodir/out"}, "");
    EXPECT_EQ(captured.err, "uniq: /nodir/out: No such file or directory\n");
    EXPECT_EQ(captured.status, 1);
}