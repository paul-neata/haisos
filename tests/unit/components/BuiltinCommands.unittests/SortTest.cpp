#include <gtest/gtest.h>
#include <algorithm>
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

TEST_F(BuiltinCommandsTest, SortGeneralNumeric) {
    // not-a-number ('' and abc) < NaN < numbers by value; hex and exponents
    // accepted; equal keys ('' vs abc) ordered by the last resort.
    const auto run = RunCaptured("sort", {"-g"}, "10\n9\n1e1\n0x10\nnan\n-inf\nabc\n\n");
    EXPECT_EQ(run.out, "\nabc\nnan\n-inf\n9\n10\n1e1\n0x10\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortHumanNumeric) {
    // First by unit (none < K < M < G), then numerically; a zero number has
    // no unit; -1G's unit is negated.
    const auto run = RunCaptured("sort", {"-h"}, "1K\n2M\n-1G\n0\n500\n1k\n-3\n");
    EXPECT_EQ(run.out, "-1G\n-3\n0\n500\n1K\n1k\n2M\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortMonth) {
    // An unknown month is 0; leading blanks skipped; JANUARY is JAN; the
    // unknown keys are equal, so the last resort orders them.
    const auto run = RunCaptured("sort", {"-M"}, "feb\nJan x\n  dec\nfoo\n");
    EXPECT_EQ(run.out, "foo\nJan x\nfeb\n  dec\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortVersion) {
    const std::string input = "foo-1.10\nfoo-1.9\nfoo-1.9.tar.gz\n.a\n..\n.\nfoo~\nfoo\n";
    const std::string expected = ".\n..\n.a\nfoo~\nfoo\nfoo-1.9\nfoo-1.9.tar.gz\nfoo-1.10\n";
    auto run = RunCaptured("sort", {"-V"}, input);
    EXPECT_EQ(run.out, expected);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    run = RunCaptured("sort", {"--version-sort"}, input);
    EXPECT_EQ(run.out, expected);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortVersionKey) {
    // -V as a key modifier, on the second dash-separated field.
    const auto run = RunCaptured("sort", {"-t-", "-k2V"}, "a-1.10\nb-1.9\n");
    EXPECT_EQ(run.out, "b-1.9\na-1.10\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortRandomGroupsEqualKeys) {
    // -R shuffles by a hash of the key, equal keys kept adjacent; the salt
    // is new each run, so only the grouping is checked, not the order.
    const std::string input = "a\nb\na\nc\nb\na\n";
    const auto run = RunCaptured("sort", {"-R"}, input);
    std::vector<std::string> lines;
    std::string line;
    for (const char c : run.out) {
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else {
            line += c;
        }
    }
    // A permutation of the input, no value split across groups.
    std::vector<std::string> sorted = lines;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(sorted, (std::vector<std::string>{"a", "a", "a", "b", "b", "c"}));
    std::vector<std::string> groups;
    for (const auto& one : lines) {
        if (groups.empty() || groups.back() != one) {
            groups.push_back(one);
        }
    }
    std::sort(groups.begin(), groups.end());
    EXPECT_EQ(groups, (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // With -u only one of each group is left.
    const auto unique = RunCaptured("sort", {"-R", "-u"}, input);
    std::vector<std::string> uniqueLines;
    std::string rest;
    for (const char c : unique.out) {
        if (c == '\n') {
            uniqueLines.push_back(rest);
            rest.clear();
        } else {
            rest += c;
        }
    }
    std::sort(uniqueLines.begin(), uniqueLines.end());
    EXPECT_EQ(uniqueLines, (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(unique.status, 0);
}

TEST_F(BuiltinCommandsTest, SortSortWord) {
    // --sort=WORD is the option it names, exact or as an unambiguous prefix.
    auto run = RunCaptured("sort", {"--sort=numeric"}, "10\n9\n");
    EXPECT_EQ(run.out, "9\n10\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    run = RunCaptured("sort", {"--sort=num"}, "10\n9\n");
    EXPECT_EQ(run.out, "9\n10\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    run = RunCaptured("sort", {"--sort=foo"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err,
        "sort: invalid argument 'foo' for '--sort'\n"
        "Valid arguments are:\n"
        "  - 'general-numeric'\n"
        "  - 'human-numeric'\n"
        "  - 'month'\n"
        "  - 'numeric'\n"
        "  - 'random'\n"
        "  - 'version'\n"
        "Try 'sort --help' for more information.\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("sort", {"--sort=h", "-n"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: options '-hn' are incompatible\n");
    EXPECT_EQ(run.status, 2);
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

TEST_F(BuiltinCommandsTest, SortDictionaryWinsOverNonprinting) {
    // GNU keeps only -d of -d and -i together, whichever order they came
    // in, so the incompatibility error says '-dn'.
    auto run = RunCaptured("sort", {"-di", "-n"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: options '-dn' are incompatible\n");
    EXPECT_EQ(run.status, 2);
    run = RunCaptured("sort", {"-id", "-n"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: options '-dn' are incompatible\n");
    EXPECT_EQ(run.status, 2);
    run = RunCaptured("sort", {"-k1di,1n"}, "x\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: options '-dn' are incompatible\n");
    EXPECT_EQ(run.status, 2);

    // -di behaves as -d alone: the same bytes ignored either way.
    const std::string input = "b-c\nb a\n";
    const auto plain = RunCaptured("sort", {"-d"}, input);
    run = RunCaptured("sort", {"-di"}, input);
    EXPECT_EQ(run.out, plain.out);
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

    // One of n g h M each, and one of d i V R: two of either set conflict.
    // f is listed in the message but conflicts with none of them.
    struct Case {
        std::vector<std::string> options;
        std::string message;
    };
    const Case cases[] = {
        {{"-gn"}, "'-gn'"},
        {{"-Mn"}, "'-Mn'"},
        {{"-nR"}, "'-nR'"},
        {{"-i", "-g"}, "'-gi'"},
        {{"-fgn"}, "'-fgn'"},
    };
    for (const auto& one : cases) {
        run = RunCaptured("sort", one.options, "x\n");
        EXPECT_EQ(run.out, kNone) << one.message;
        EXPECT_EQ(run.err, "sort: options " + one.message + " are incompatible\n") << one.message;
        EXPECT_EQ(run.status, 2) << one.message;
    }
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

// --- Checking and merging ---

TEST_F(BuiltinCommandsTest, SortCheck) {
    // The first disorder, by name, line number and the line itself, the
    // delimiter closing the message.
    auto run = RunCaptured("sort", {"-c", "-"}, "a\nb\n\nc");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: -:3: disorder: \n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("sort", {"-c", "-"}, "a\nb\nc\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // -C and --check=silent check quietly.
    run = RunCaptured("sort", {"-C", "-"}, "b\na\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 1);
    run = RunCaptured("sort", {"--check=silent", "-"}, "b\na\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 1);

    // -cu asks for strict ordering: an equal line is a disorder too.
    run = RunCaptured("sort", {"-cu", "-"}, "a\na\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: -:2: disorder: a\n");
    EXPECT_EQ(run.status, 1);

    // The last resort is part of the check: folded-equal lines disordered
    // by their raw bytes.
    run = RunCaptured("sort", {"-c", "-f", "-"}, "a\nA\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: -:2: disorder: A\n");
    EXPECT_EQ(run.status, 1);

    // Keys decide, not whole lines.
    run = RunCaptured("sort", {"-c", "-k2n", "-"}, "x 2\ny 10\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    run = RunCaptured("sort", {"-c", "-k2", "-"}, "x 2\ny 10\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: -:2: disorder: y 10\n");
    EXPECT_EQ(run.status, 1);

    // -z's records end the message with a NUL.
    run = RunCaptured("sort", {"-cz", "-"}, std::string("b\0a\0", 4));
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, std::string("sort: -:2: disorder: a\0", 23));
    EXPECT_EQ(run.status, 1);

    // A file operand is named as given.
    WriteFile("/f.txt", "b\na\n");
    run = RunCaptured("sort", {"-c", "f.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: f.txt:2: disorder: a\n");
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, SortCheckErrors) {
    WriteFile("/f.txt", "a\n");

    // More than one input, the second operand named; no Try line.
    auto run = RunCaptured("sort", {"-c", "f.txt", "g"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: extra operand 'g' not allowed with -c\n");
    EXPECT_EQ(run.status, 2);

    // -C names itself in the extra-operand error.
    run = RunCaptured("sort", {"-C", "f.txt", "g"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: extra operand 'g' not allowed with -C\n");
    EXPECT_EQ(run.status, 2);

    run = RunCaptured("sort", {"-c", "-o", "z", "f.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: options '-co' are incompatible\n");
    EXPECT_EQ(run.status, 2);

    // Either order of the two check modes says '-cC'.
    run = RunCaptured("sort", {"-C", "-c", "f.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: options '-cC' are incompatible\n");
    EXPECT_EQ(run.status, 2);
    run = RunCaptured("sort", {"-c", "-C", "f.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: options '-cC' are incompatible\n");
    EXPECT_EQ(run.status, 2);

    run = RunCaptured("sort", {"--check=foo", "f.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err,
        "sort: invalid argument 'foo' for '--check'\n"
        "Valid arguments are:\n"
        "  - 'quiet', 'silent'\n"
        "  - 'diagnose-first'\n"
        "Try 'sort --help' for more information.\n");
    EXPECT_EQ(run.status, 1);

    // An input that cannot be opened is an "open failed", not a "cannot
    // read" as a sorting run's is.
    run = RunCaptured("sort", {"-c", "nofile"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: open failed: nofile: No such file or directory\n");
    EXPECT_EQ(run.status, 2);
}

TEST_F(BuiltinCommandsTest, SortMerge) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    WriteFile("/w/x", "a\nb\n");
    WriteFile("/w/y", "a\nc\n");
    WriteFile("/w/u", "b\na\n");

    // The inputs merged as they stand, equal head lines going to the
    // earlier input.
    auto run = RunCaptured("sort", {"-m", "/w/x", "/w/y"});
    EXPECT_EQ(run.out, "a\na\nb\nc\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // With -u, a line equal to the last one output is dropped.
    run = RunCaptured("sort", {"-mu", "/w/x", "/w/y"});
    EXPECT_EQ(run.out, "a\nb\nc\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // Never re-sorted: two copies of an unsorted input pass through.
    run = RunCaptured("sort", {"-m", "/w/u", "/w/u"});
    EXPECT_EQ(run.out, "b\na\nb\na\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // A missing input is a read error, as in a sorting run.
    run = RunCaptured("sort", {"-m", "/w/x", "nofile"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "sort: cannot read: nofile: No such file or directory\n");
    EXPECT_EQ(run.status, 2);
}

TEST_F(BuiltinCommandsTest, SortVersionIsNotVersionSort) {
    auto run = RunCaptured("sort", {"--version"});
    EXPECT_EQ(run.out, "sort (HaisosOS builtin) 1.1.0\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // The exact long name wins over the prefix: --version-sort is -V, and
    // sorts by version.
    run = RunCaptured("sort", {"--version-sort"}, "b\na\n");
    EXPECT_EQ(run.out, "a\nb\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, SortIgnoredSizeOptions) {
    const auto run = RunCaptured("sort",
        {"-S", "1M", "-T", "/tmp", "--parallel=2", "--batch-size=4"}, "b\na\n");
    EXPECT_EQ(run.out, "a\nb\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}