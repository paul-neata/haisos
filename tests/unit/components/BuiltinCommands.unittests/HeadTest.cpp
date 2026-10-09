#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"

namespace Haisos {

TEST_F(BuiltinCommandsTest, HeadLinesAndBytes) {
    WriteFile("/n12", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n");
    WriteFile("/abc", "a\nb\nc");
    // The obsolete -NUM, and -n with a leading '-': all but the last NUM.
    EXPECT_EQ(RunCaptured("head", {"-3", "/n12"}).out, "1\n2\n3\n");
    EXPECT_EQ(RunCaptured("head", {"-n", "-9", "/n12"}).out, "1\n2\n3\n");
    EXPECT_EQ(RunCaptured("head", {"-c", "5", "/n12"}).out, "1\n2\n3");
    EXPECT_EQ(RunCaptured("head", {"-c", "-20", "/n12"}).out, "1\n2\n3\n4");
    EXPECT_EQ(RunCaptured("head", {"-c", "1k", "/n12"}).out,
        "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n");
    EXPECT_EQ(RunCaptured("head", {"-5c", "/n12"}).out, "1\n2\n3");
    EXPECT_EQ(RunCaptured("head", {"-n", "0", "/n12"}).out, "");
    EXPECT_EQ(RunCaptured("head", {"-c", "-0", "/abc"}).out, "a\nb\nc");
    EXPECT_EQ(RunCaptured("head", {"-n", "-1", "/abc"}).out, "a\nb\n");
    // No FILE: the standard input.
    EXPECT_EQ(RunCaptured("head", {"-n", "2"}, "1\n2\n3\n").out, "1\n2\n");
}

TEST_F(BuiltinCommandsTest, HeadHeaders) {
    WriteFile("/n12", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n");
    WriteFile("/abc", "a\nb\nc");
    // More than one FILE: a header before each, a blank line between them.
    EXPECT_EQ(RunCaptured("head", {"-n", "2", "/n12", "/abc"}).out,
        "==> /n12 <==\n1\n2\n\n==> /abc <==\na\nb\n");
    // -q never names the files, even with several.
    EXPECT_EQ(RunCaptured("head", {"-q", "-n1", "/n12", "/abc"}).out, "1\na\n");
    // -v always does, even with one.
    EXPECT_EQ(RunCaptured("head", {"-v", "-n1", "/abc"}).out, "==> /abc <==\na\n");
    // The obsolete spelling takes -q and -v too.
    EXPECT_EQ(RunCaptured("head", {"-2v", "/n12"}).out, "==> /n12 <==\n1\n2\n");
    EXPECT_EQ(RunCaptured("head", {"-2q", "/n12", "/abc"}).out, "1\n2\na\nb\n");
    EXPECT_EQ(RunCaptured("head", {"-n1", "-", "/n12"}, "a\nb\n").out,
        "==> standard input <==\na\n\n==> /n12 <==\n1\n");
}

TEST_F(BuiltinCommandsTest, HeadZeroTerminated) {
    // -z: the delimiter is NUL, so the last entry without one is still a line.
    EXPECT_EQ(RunCaptured("head", {"-z", "-n", "2"}, std::string("a\0b\0c\0", 6)).out,
        std::string("a\0b\0", 4));
    EXPECT_EQ(RunCaptured("head", {"-z", "-n", "2"}, std::string("a\0b\0c", 5)).out,
        std::string("a\0b\0", 4));
}

TEST_F(BuiltinCommandsTest, HeadErrors) {
    WriteFile("/n12", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n");
    WriteFile("/abc", "a\nb\nc");
    // A FILE that cannot be read is reported and skipped; the others still
    // print, under their headers, and the exit status is 1.
    Captured captured = RunCaptured("head", {"-n", "1", "/missing", "/n12"});
    EXPECT_EQ(captured.out, "==> /n12 <==\n1\n");
    EXPECT_EQ(captured.err, "head: cannot open '/missing' for reading: No such file or directory\n");
    EXPECT_EQ(captured.status, 1);

    // A directory opens and fails on read, as GNU's does.
    captured = RunCaptured("head", {"-n1", "/n12", "/docs", "/abc"});
    EXPECT_EQ(captured.out, "==> /n12 <==\n1\n\n==> /docs <==\n\n==> /abc <==\na\n");
    EXPECT_EQ(captured.err, "head: error reading '/docs': Is a directory\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("head", {"-n", "x", "/n12"});
    EXPECT_EQ(captured.err, "head: invalid number of lines: 'x'\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("head", {"-c", "", "/n12"});
    EXPECT_EQ(captured.err, "head: invalid number of bytes: ''\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("head", {"-n", "99999999999999999999", "/n12"});
    EXPECT_EQ(captured.err,
        "head: invalid number of lines: '99999999999999999999': Value too large for defined data type\n");
    EXPECT_EQ(captured.status, 1);

    // The same value through the obsolete spelling, and one with a bad
    // trailing letter.
    captured = RunCaptured("head", {"-99999999999999999999", "/n12"});
    EXPECT_EQ(captured.err,
        "head: invalid number of lines: '99999999999999999999': Value too large for defined data type\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("head", {"-5x", "/n12"});
    EXPECT_EQ(captured.err, "head: invalid trailing option -- x\nTry 'head --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    // A -NUM given where the obsolete form does not reach it.
    captured = RunCaptured("head", {"-n1", "-5", "/n12"});
    EXPECT_EQ(captured.err, "head: invalid trailing option -- 5\nTry 'head --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
}

} // namespace Haisos