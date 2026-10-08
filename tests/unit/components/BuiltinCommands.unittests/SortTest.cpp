#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"

using namespace Haisos;

namespace {

const std::string kNone;

} // namespace

// --- The orderings ---

TEST_F(BuiltinCommandsTest, SortDefaultIsByteOrder) {
    const auto run = RunCaptured("sort", {}, "b\nB\na\n10\n9\n");
    EXPECT_EQ(run.out, "10\n9\nB\na\nb\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortReadsLinesLongerThanOneRead) {
    // A line of 200000 bytes spans several 64 KiB reads.
    const std::string longLine(200000, 'b');
    const auto run = RunCaptured("sort", {}, longLine + "\nc\na\n" + longLine + "x\n");
    EXPECT_EQ(run.out, "a\n" + longLine + "\n" + longLine + "x\nc\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortAddsAMissingFinalNewline) {
    const auto run = RunCaptured("sort", {}, "b\na");
    EXPECT_EQ(run.out, "a\nb\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortReverseAndUnique) {
    auto run = RunCaptured("sort", {"-r"}, "a\nc\nb\n");
    EXPECT_EQ(run.out, "c\nb\na\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    run = RunCaptured("sort", {"-u"}, "b\na\nb\n");
    EXPECT_EQ(run.out, "a\nb\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortFoldCaseUniqueKeepsFirstOfRun) {
    // With -u the comparison is by keys only: the first of each equal run in
    // input order is the one kept.
    auto run = RunCaptured("sort", {"-fu"}, "b\nB\na\nA\n");
    EXPECT_EQ(run.out, "a\nb\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    // Without -u the last resort separates the folded equals.
    run = RunCaptured("sort", {"-f"}, "b\nB\na\nA\n");
    EXPECT_EQ(run.out, "A\na\nB\nb\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    // Bytes compare unsigned: 0xC3 (of e-acute) sorts after every ASCII letter.
    run = RunCaptured("sort", {"-f"}, "\xc3\xa9\nb\na\n");
    EXPECT_EQ(run.out, "a\nb\n\xc3\xa9\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortNumeric) {
    auto run = RunCaptured("sort", {"-n"}, "-0\n0\n+1\n.5\n-.5\n1,000\n 2\n");
    EXPECT_EQ(run.out, "-.5\n+1\n-0\n0\n.5\n1,000\n 2\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    run = RunCaptured("sort", {"-rn"}, "1\n10\n9\n");
    EXPECT_EQ(run.out, "10\n9\n1\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    // 40-digit numbers compare exactly, never through a float.
    const std::string nines(40, '9');
    const std::string tenPow40 = "1" + std::string(40, '0');
    run = RunCaptured("sort", {"-n"}, tenPow40 + "\n" + nines + "\n");
    EXPECT_EQ(run.out, nines + "\n" + tenPow40 + "\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortKeysWithSeparator) {
    const auto run = RunCaptured("sort", {"-t:", "-k1,1", "-k2,2n"}, "b:2\na:10\nb:1\n");
    EXPECT_EQ(run.out, "a:10\nb:1\nb:2\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortDefaultFieldsKeepLeadingBlanks) {
    // A default field includes its leading blanks, so "  b" sorts before " b".
    auto run = RunCaptured("sort", {"-k2"}, "a b\na  b\n");
    EXPECT_EQ(run.out, "a  b\na b\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    // With b the keys are equal ("b" == "b") and the last resort decides:
    // "a  b" < "a b" at the second space.
    run = RunCaptured("sort", {"-k2b"}, "a b\na  b\n");
    EXPECT_EQ(run.out, "a  b\na b\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortKeyCharacterPositions) {
    auto run = RunCaptured("sort", {"-k1.2,1.3"}, "xbc\nyab\n");
    EXPECT_EQ(run.out, "yab\nxbc\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    run = RunCaptured("sort", {"-t,", "-k2.2"}, "x,ab\ny,aa\n");
    EXPECT_EQ(run.out, "y,aa\nx,ab\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortKeyModifiersOverrideGlobals) {
    // A key with no modifier inherits -r; one with any modifier takes none of
    // the global options.
    auto run = RunCaptured("sort", {"-r", "-k1,1"}, "b 1\na 2\nc 10\n");
    EXPECT_EQ(run.out, "c 10\nb 1\na 2\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    run = RunCaptured("sort", {"-r", "-k2,2n"}, "b 1\na 2\nc 10\n");
    EXPECT_EQ(run.out, "b 1\na 2\nc 10\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortStableKeepsInputOrder) {
    // -s disables the last resort, so equal keys keep their input order.
    auto run = RunCaptured("sort", {"-s", "-k1,1"}, "a 2\nb 1\na 1\n");
    EXPECT_EQ(run.out, "a 2\na 1\nb 1\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    run = RunCaptured("sort", {"-k1,1"}, "a 2\nb 1\na 1\n");
    EXPECT_EQ(run.out, "a 1\na 2\nb 1\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortDictionaryAndNonprinting) {
    auto run = RunCaptured("sort", {"-d"}, "b-c\nb a\n");
    EXPECT_EQ(run.out, "b a\nb-c\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    run = RunCaptured("sort", {"-i"}, "a\001c\nab\n");
    EXPECT_EQ(run.out, "ab\na\001c\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortZeroTerminated) {
    auto run = RunCaptured("sort", {"-z"}, std::string("b\0a\0", 4));
    EXPECT_EQ(run.out, std::string("a\0b\0", 4));
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    // A newline is a blank too: it splits fields inside a -z record.
    run = RunCaptured("sort", {"-z", "-k2"}, std::string("a\nz\0b\ny\0", 8));
    EXPECT_EQ(run.out, std::string("b\ny\0a\nz\0", 8));
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

// --- Inputs and outputs ---

TEST_F(BuiltinCommandsTest, SortOutputMayBeAnInput) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/f", "3\n1\n2\n");
    const auto run = RunCaptured("sort", {"-o", "f", "f"}, std::nullopt, "/w");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/w/f", content));
    EXPECT_EQ(content, "1\n2\n3\n");
}

TEST_F(BuiltinCommandsTest, SortSeveralFilesAndStdin) {
    ASSERT_EQ(root->CreateDirectory("/in", kDirMode), 0);
    WriteFile("/in/a.txt", "a\nc\n");
    WriteFile("/in/b.txt", "b\n");
    const auto run = RunCaptured("sort", {"/in/a.txt", "-", "/in/b.txt"}, "d\n");
    EXPECT_EQ(run.out, "a\nb\nc\nd\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortFiles0From) {
    ASSERT_EQ(root->CreateDirectory("/in", kDirMode), 0);
    WriteFile("/in/a.txt", "a\nc\n");
    WriteFile("/in/b.txt", "b\n");
    WriteFile("/list", std::string("/in/a.txt\0/in/b.txt\0", 20));
    auto run = RunCaptured("sort", {"--files0-from=/list"});
    EXPECT_EQ(run.out, "a\nb\nc\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // A trailing partial name after the last NUL is a name too.
    WriteFile("/list2", std::string("/in/a.txt\0/in/b.txt", 19));
    run = RunCaptured("sort", {"--files0-from=/list2"});
    EXPECT_EQ(run.out, "a\nb\nc\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // The names may come from standard input.
    run = RunCaptured("sort", {"--files0-from=-"}, std::string("/in/a.txt\0/in/b.txt\0", 20));
    EXPECT_EQ(run.out, "a\nb\nc\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // Operands too: an extra operand, named, and the Try line.
    run = RunCaptured("sort", {"--files0-from=/list", "x"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: extra operand 'x'\nfile operands cannot be combined with --files0-from\nTry 'sort --help' for more information.\n");
    EXPECT_EQ(run.status, 2);

    // A missing list file.
    run = RunCaptured("sort", {"--files0-from=/nolist"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: open failed: /nolist: No such file or directory\n");
    EXPECT_EQ(run.status, 2);

    // A list file that is a directory.
    run = RunCaptured("sort", {"--files0-from=/docs"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: cannot read file names from '/docs'\n");
    EXPECT_EQ(run.status, 2);

    // An empty list: no input at all.
    WriteFile("/empty", "");
    run = RunCaptured("sort", {"--files0-from=/empty"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: no input from '/empty'\n");
    EXPECT_EQ(run.status, 2);

    // An empty name, by its 1-based number in the list.
    WriteFile("/list3", std::string("/in/a.txt\0\0", 11));
    run = RunCaptured("sort", {"--files0-from=/list3"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: /list3:2: invalid zero-length file name\n");
    EXPECT_EQ(run.status, 2);

    // The same from standard input, named "-".
    run = RunCaptured("sort", {"--files0-from=-"}, std::string("\0", 1));
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: -:1: invalid zero-length file name\n");
    EXPECT_EQ(run.status, 2);

    // A name "-" while the names come from standard input.
    run = RunCaptured("sort", {"--files0-from=-"}, std::string("-\0", 2));
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: when reading file names from stdin, no file name of '-' allowed\n");
    EXPECT_EQ(run.status, 2);

    // "-" in a list file is fine: it reads standard input.
    WriteFile("/list4", std::string("-\0", 2));
    run = RunCaptured("sort", {"--files0-from=/list4"}, "b\na\n");
    EXPECT_EQ(run.out, "a\nb\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

// --- The diagnostics ---

TEST_F(BuiltinCommandsTest, SortKeyErrors) {
    struct Case {
        std::string keydef;
        std::string message;
    };
    const Case cases[] = {
        {"0", "field number is zero: invalid field specification '0'"},
        {"1.0", "character offset is zero: invalid field specification '1.0'"},
        {"a", "invalid number at field start: invalid count at start of 'a'"},
        {"1,a", "invalid number after ',': invalid count at start of 'a'"},
        {"1.a", "invalid number after '.': invalid count at start of 'a'"},
        {"1x", "stray character in field spec: invalid field specification '1x'"},
        {"1,0", "field number is zero: invalid field specification '1,0'"},
        {"1,2.", "invalid number after '.': invalid count at start of ''"},
        {"1,,", "invalid number after ',': invalid count at start of ','"},
        {"+a", "invalid number at field start: invalid count at start of '+a'"},
    };
    for (const auto& one : cases) {
        const auto run = RunCaptured("sort", {"-k", one.keydef}, "x\n");
        EXPECT_EQ(run.out, kNone) << one.keydef;
        EXPECT_EQ(run.err, "sort: " + one.message + "\n") << one.keydef;
        EXPECT_EQ(run.status, 2) << one.keydef;
    }
    // A huge field number is accepted, saturating: it sorts by an empty key.
    auto run = RunCaptured("sort", {"-k", "99999999999999999999"}, "b\na\n");
    EXPECT_EQ(run.out, "a\nb\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortTabErrors) {
    auto run = RunCaptured("sort", {"-t", "ab"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: multi-character tab 'ab'\n");
    EXPECT_EQ(run.status, 2);

    run = RunCaptured("sort", {"-t", ""}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: empty tab\n");
    EXPECT_EQ(run.status, 2);

    run = RunCaptured("sort", {"-t", "a", "-t", "b"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: incompatible tabs\n");
    EXPECT_EQ(run.status, 2);

    // The same tab twice is fine, and '\0' (backslash, zero) is the NUL byte:
    // the second fields x < y decide, not the whole lines.
    run = RunCaptured("sort", {"-t", "a", "-t", "a"}, "b x\na y\n");
    EXPECT_EQ(run.out, "a y\nb x\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    run = RunCaptured("sort", {"-t", "\\0", "-k2,2"}, std::string("b\0x\na\0y\n", 8));
    EXPECT_EQ(run.out, std::string("b\0x\na\0y\n", 8));
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortIncompatibleOptions) {
    auto run = RunCaptured("sort", {"-dn"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: options '-dn' are incompatible\n");
    EXPECT_EQ(run.status, 2);

    run = RunCaptured("sort", {"-k1,1in"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: options '-in' are incompatible\n");
    EXPECT_EQ(run.status, 2);

    run = RunCaptured("sort", {"-k1,1Vn"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: options '-nV' are incompatible\n");
    EXPECT_EQ(run.status, 2);
}

TEST_F(BuiltinCommandsTest, SortReadErrors) {
    auto run = RunCaptured("sort", {"nofile"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: cannot read: nofile: No such file or directory\n");
    EXPECT_EQ(run.status, 2);

    run = RunCaptured("sort", {"/docs"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: read failed: /docs: Is a directory\n");
    EXPECT_EQ(run.status, 2);

    // A read failure aborts the later inputs too, before anything is printed.
    run = RunCaptured("sort", {"nofile", "/notes.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: cannot read: nofile: No such file or directory\n");
    EXPECT_EQ(run.status, 2);
}

TEST_F(BuiltinCommandsTest, SortOutputErrors) {
    auto run = RunCaptured("sort", {"-o", "/docs"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: open failed: /docs: Is a directory\n");
    EXPECT_EQ(run.status, 2);

    run = RunCaptured("sort", {"-o", "/nodir/x"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: open failed: /nodir/x: No such file or directory\n");
    EXPECT_EQ(run.status, 2);

    run = RunCaptured("sort", {"-o", "a", "-o", "b"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: multiple output files specified\n");
    EXPECT_EQ(run.status, 2);
}

TEST_F(BuiltinCommandsTest, SortUsageErrors) {
    auto run = RunCaptured("sort", {"-X"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: invalid option -- 'X'\nTry 'sort --help' for more information.\n");
    EXPECT_EQ(run.status, 2);

    run = RunCaptured("sort", {"-k"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: option requires an argument -- 'k'\nTry 'sort --help' for more information.\n");
    EXPECT_EQ(run.status, 2);
}

TEST_F(BuiltinCommandsTest, SortVersionIsNotVersionSort) {
    auto run = RunCaptured("sort", {"--version"});
    EXPECT_EQ(run.out, "sort (HaisosOS builtin) 1.0.0\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // The exact long name wins over the prefix: --version-sort is -V, not
    // treated, and still sorts.
    run = RunCaptured("sort", {"--version-sort"}, "b\na\n");
    EXPECT_EQ(run.out, "a\nb\n");
    EXPECT_EQ(run.err, "Parameter --version-sort is not treated by HaisosOS sort v. 1.0.0\n");
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortIgnoredSizeOptions) {
    const auto run = RunCaptured("sort",
        {"-S", "1M", "-T", "/tmp", "--parallel=2", "--batch-size=4"}, "b\na\n");
    EXPECT_EQ(run.out, "a\nb\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}