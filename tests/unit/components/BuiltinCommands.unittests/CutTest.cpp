#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "src/components/Filesystem/FilesystemUtils.h"

using namespace Haisos;

namespace {

// The plan's lines: a delimited one, one with no delimiter, one whose fields
// are empty, one with no delimiters at all.
const std::string kLines = "a:b:c\nnodelim\n:x:\nabcdef\n";

const char* kTryCut = "Try 'cut --help' for more information.\n";

} // namespace

// --- cut ---

TEST_F(BuiltinCommandsTest, CutFields) {
    Captured captured = RunCaptured("cut", {"-d:", "-f2"}, kLines);
    EXPECT_EQ(captured.out, "b\nnodelim\nx\nabcdef\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("cut", {"-d:", "-f2", "-s"}, kLines);
    EXPECT_EQ(captured.out, "b\nx\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("cut", {"-d:", "-f1,3", "--output-delimiter=--"}, kLines);
    EXPECT_EQ(captured.out, "a--c\nnodelim\n--\nabcdef\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("cut", {"-d:", "-f2", "--complement"}, kLines);
    EXPECT_EQ(captured.out, "a:c\nnodelim\n:\nabcdef\n");
    EXPECT_EQ(captured.status, 0);

    // 1-2,2-3 overlap and merge into 1-3, so every field is its own piece.
    captured = RunCaptured("cut", {"-d:", "-f", "1-2,2-3", "--output-delimiter=X"}, kLines);
    EXPECT_EQ(captured.out, "aXbXc\nnodelim\nXxX\nabcdef\n");
    EXPECT_EQ(captured.status, 0);

    // Blanks separate items like commas do, and the fields print in line
    // order whatever order the list gave them.
    captured = RunCaptured("cut", {"-d:", "-f", "1 3"}, kLines);
    EXPECT_EQ(captured.out, "a:c\nnodelim\n:\nabcdef\n");
    captured = RunCaptured("cut", {"-d:", "-f", "3,1"}, kLines);
    EXPECT_EQ(captured.out, "a:c\nnodelim\n:\nabcdef\n");

    captured = RunCaptured("cut", {"-f2"}, "a\tb\n");
    EXPECT_EQ(captured.out, "b\n");

    // A field past the line's end is simply absent: an empty line out.
    captured = RunCaptured("cut", {"-d:", "-f3"}, "a:b");
    EXPECT_EQ(captured.out, "\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, CutBytes) {
    Captured captured = RunCaptured("cut", {"-b", "2-3,5-"}, kLines);
    EXPECT_EQ(captured.out, ":bc\nodlim\nx:\nbcef\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("cut", {"-b", "-2"}, kLines);
    EXPECT_EQ(captured.out, "a:\nno\n:x\nab\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("cut", {"-b", "1,3", "--output-delimiter=X"}, kLines);
    EXPECT_EQ(captured.out, "aXb\nnXd\n:X:\naXc\n");
    EXPECT_EQ(captured.status, 0);

    // 1-2 and 4-5 do not overlap, so the delimiter goes between them.
    captured = RunCaptured("cut", {"-b", "1-2,4-5", "--output-delimiter=X"}, kLines);
    EXPECT_EQ(captured.out, "a:X:c\nnoXel\n:x\nabXde\n");
    EXPECT_EQ(captured.status, 0);

    // 1-2 and 2-4 do overlap and merge into 1-4: one piece, no delimiter.
    captured = RunCaptured("cut", {"-b", "1-2,2-4", "--output-delimiter=X"}, kLines);
    EXPECT_EQ(captured.out, "a:b:\nnode\n:x:\nabcd\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("cut", {"-b", "2,4", "--complement", "--output-delimiter=X"}, "abcdef\n");
    EXPECT_EQ(captured.out, "aXcXef\n");
    EXPECT_EQ(captured.status, 0);

    // -c counts bytes, as GNU cut does.
    captured = RunCaptured("cut", {"-c1"}, "\xC3\xA9\n");
    EXPECT_EQ(captured.out, "\xC3\n");
    EXPECT_EQ(captured.status, 0);

    // -n is ignored, as GNU ignores it.
    captured = RunCaptured("cut", {"-n", "-b1"}, kLines);
    EXPECT_EQ(captured.out, "a\nn\n:\na\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, CutZeroTerminated) {
    // One NUL-terminated "line": the whole text, its final newline kept.
    const Captured captured = RunCaptured("cut", {"-z", "-f1"}, kLines);
    EXPECT_EQ(captured.out, kLines + std::string(1, '\0'));
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, CutUsageErrors) {
    const std::vector<std::vector<std::string>> cases = {
        {"-s", "-b1"},
        {"-f0"},
        {"-f", "3-1"},
        {"-f", "x"},
        {"-f", ""},
        {"-f", "1-2-3"},
        {"-f", "-"},
        {"-f", "1x"},
        {"-f", "x-1"},
        {"-b", "x"},
        {"-c", "0"},
        {"-b", "1-2-3"},
        {"-f", "99999999999999999999"},
        {"-b", "1", "-d:"},
        {"-f1", "-b1"},
        {"-d", "ab", "-f1"},
    };
    const std::vector<std::string> messages = {
        "suppressing non-delimited lines makes sense\n\tonly when operating on fields",
        "fields are numbered from 1",
        "invalid decreasing range",
        "invalid field value 'x'",
        "fields are numbered from 1",
        "invalid field range",
        "invalid range with no endpoint: -",
        "invalid field value 'x'",
        "invalid field value 'x-1'",
        "invalid byte/character position 'x'",
        "byte/character positions are numbered from 1",
        "invalid byte or character range",
        "field number '99999999999999999999' is too large",
        "an input delimiter may be specified only when operating on fields",
        "only one list may be specified",
        "the delimiter must be a single character",
    };
    ASSERT_EQ(cases.size(), messages.size());
    for (size_t i = 0; i < cases.size(); ++i) {
        const Captured captured = RunCaptured("cut", cases[i], kLines);
        EXPECT_EQ(captured.out, "") << messages[i];
        EXPECT_EQ(captured.err, "cut: " + messages[i] + "\n" + kTryCut) << messages[i];
        EXPECT_EQ(captured.status, 1) << messages[i];
    }
    // With no list at all, before any file is looked at.
    const Captured none = RunCaptured("cut", {}, kLines);
    EXPECT_EQ(none.err, "cut: you must specify a list of bytes, characters, or fields\n" + std::string(kTryCut));
    EXPECT_EQ(none.status, 1);
}

TEST_F(BuiltinCommandsTest, CutFileErrors) {
    WriteFile("/c.txt", "content\n");
    const Captured captured = RunCaptured("cut", {"-b1", "/docs", "nofile", "c.txt"});
    EXPECT_EQ(captured.err, "cut: /docs: Is a directory\ncut: nofile: No such file or directory\n");
    EXPECT_EQ(captured.out, "c\n");
    EXPECT_EQ(captured.status, 1);
}