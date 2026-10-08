#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "commands/grep/GrepMatcher.h"
#include "src/components/Filesystem/FilesystemUtils.h"

using namespace Haisos;

namespace {

// The plan's a.c: 64 bytes, three TODO lines (2, 5 and 10).
const std::string kAc =
    "one\ntwo TODO\nthree\nfour\nfive TODO\nsix\nseven\neight\nnine\nten TODO\n";

void WriteTo(const std::shared_ptr<IFileSystem>& fs, const std::string& path, const std::string& content) {
    auto file = fs->OpenFile(path, kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(file, nullptr) << path;
    file->Write(content.data(), content.size());
}

// /g with the plan's files: a.c, b.h, t1, k (no TODO), a pattern file, an
// empty one and a binary one.
void MakeGrepFiles(const std::shared_ptr<IFileSystem>& fs) {
    ASSERT_EQ(fs->CreateDirectory("/g", kDirMode), 0);
    WriteTo(fs, "/g/a.c", kAc);
    WriteTo(fs, "/g/b.h", "TODO sub\n");
    WriteTo(fs, "/g/t1", "TODO\n");
    WriteTo(fs, "/g/k", "nothing here\n");
    WriteTo(fs, "/g/pat", "two\nten\n");
    WriteTo(fs, "/g/empty", "");
    WriteTo(fs, "/g/bin", std::string("x\0y TODO\n", 9));
}

// /g with the plan's recursive-search tree, on its own (beside MakeGrepFiles'
// flat files): src/a.c (kAc), src/sub/b.h, src/bin.dat (binary), t1 and a
// hidden .hid/h.c -- so a walk of /g lists them in byte order:
// .hid/h.c, src/a.c, src/bin.dat, src/sub/b.h, t1.
void MakeGrepTree(const std::shared_ptr<IFileSystem>& fs) {
    ASSERT_EQ(fs->CreateDirectory("/g", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/g/.hid", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/g/src", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/g/src/sub", kDirMode), 0);
    WriteTo(fs, "/g/.hid/h.c", "TODO hidden\n");
    WriteTo(fs, "/g/src/a.c", kAc);
    WriteTo(fs, "/g/src/bin.dat", std::string("x\0y TODO\n", 9));
    WriteTo(fs, "/g/src/sub/b.h", "TODO sub\n");
    WriteTo(fs, "/g/t1", "TODO\n");
}

} // namespace

// --- grep: matching and output ---

TEST_F(BuiltinCommandsTest, GrepPrintsMatchingLines) {
    MakeGrepFiles(root);
    const Captured captured = RunCaptured("grep", {"TODO", "/g/a.c"});
    EXPECT_EQ(captured.out, "two TODO\nfive TODO\nten TODO\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, GrepNoMatchExitsOne) {
    MakeGrepFiles(root);
    const Captured captured = RunCaptured("grep", {"nope", "/g/a.c"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, GrepMissingFileExitsTwo) {
    MakeGrepFiles(root);
    const Captured captured = RunCaptured("grep", {"x", "nosuch"});
    EXPECT_EQ(captured.err, "grep: nosuch: No such file or directory\n");
    EXPECT_EQ(captured.status, 2);
    // -s hides the message but keeps the trouble.
    const Captured quiet = RunCaptured("grep", {"-s", "x", "nosuch"});
    EXPECT_EQ(quiet.err, "");
    EXPECT_EQ(quiet.status, 2);
    // -q exits 0 at the first selected line, even after an error.
    const Captured early = RunCaptured("grep", {"-q", "TODO", "nosuch", "/g/a.c"});
    EXPECT_EQ(early.err, "grep: nosuch: No such file or directory\n");
    EXPECT_EQ(early.status, 0);
}

TEST_F(BuiltinCommandsTest, GrepDirectoryWithoutRecursion) {
    MakeGrepFiles(root);
    const Captured captured = RunCaptured("grep", {"TODO", "/g"});
    EXPECT_EQ(captured.err, "grep: /g: Is a directory\n");
    EXPECT_EQ(captured.status, 2);
}

TEST_F(BuiltinCommandsTest, GrepSeveralFilesShowNames) {
    MakeGrepFiles(root);
    const Captured captured = RunCaptured("grep", {"-c", "TODO", "a.c", "b.h"}, "", "/g");
    EXPECT_EQ(captured.out, "a.c:3\nb.h:1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    EXPECT_EQ(RunCaptured("grep", {"-h", "-c", "TODO", "a.c", "b.h"}, "", "/g").out, "3\n1\n");
    EXPECT_EQ(RunCaptured("grep", {"-H", "TODO", "t1"}, "", "/g").out, "t1:TODO\n");
}

TEST_F(BuiltinCommandsTest, GrepPrefixes) {
    MakeGrepFiles(root);
    const Captured captured = RunCaptured("grep", {"-nbH", "TODO", "a.c"}, "", "/g");
    EXPECT_EQ(captured.out, "a.c:2:4:two TODO\na.c:5:24:five TODO\na.c:10:55:ten TODO\n");
    EXPECT_EQ(captured.status, 0);
    // -o with -b: the offset of each match.
    EXPECT_EQ(RunCaptured("grep", {"-ob", "TODO", "a.c"}, "", "/g").out, "8:TODO\n29:TODO\n59:TODO\n");
}

TEST_F(BuiltinCommandsTest, GrepByteOffsetsPastTheFirstRead) {
    MakeGrepFiles(root);
    // 50000 lines "x\n" (100000 bytes, past one 96 KiB read), then TODO: its
    // offset counts every byte before it, whichever read brought them.
    std::string big;
    for (int i = 0; i < 50000; ++i) {
        big += "x\n";
    }
    big += "TODO\n";
    WriteTo(root, "/g/big", big);
    EXPECT_EQ(RunCaptured("grep", {"-nb", "TODO", "big"}, "", "/g").out, "50001:100000:TODO\n");
    EXPECT_EQ(RunCaptured("grep", {"-ob", "TODO"}, big).out, "100000:TODO\n");
}

TEST_F(BuiltinCommandsTest, GrepInitialTab) {
    MakeGrepFiles(root);
    // a.c is 64 bytes: line numbers pad to the digits of 65, offsets to the
    // digits of 64; b.h is 9 bytes: both to the digits of 10.
    const Captured captured = RunCaptured("grep", {"-T", "-n", "TODO", "a.c", "b.h"}, "", "/g");
    EXPECT_EQ(captured.out,
        "a.c: 2:\ttwo TODO\na.c: 5:\tfive TODO\na.c:10:\tten TODO\nb.h: 1:\tTODO sub\n");
    EXPECT_EQ(captured.status, 0);
    // t1 is 5 bytes: no padding at all, but the tab after the name.
    EXPECT_EQ(RunCaptured("grep", {"-T", "-H", "TODO", "t1"}, "", "/g").out, "t1:\tTODO\n");
    // A descriptor has no size, so the standard input always pads to 19.
    const Captured piped = RunCaptured("grep", {"-Tn", "TODO"}, "TODO\n");
    EXPECT_EQ(piped.out, std::string(18, ' ') + "1:\tTODO\n");
}

TEST_F(BuiltinCommandsTest, GrepStdinAndLabel) {
    MakeGrepFiles(root);
    const Captured label = RunCaptured("grep", {"-H", "--label=foo", "-c", "TODO"}, kAc);
    EXPECT_EQ(label.out, "foo:3\n");
    EXPECT_EQ(label.status, 0);
    const Captured both = RunCaptured("grep", {"-c", "TODO", "-", "t1"}, kAc, "/g");
    EXPECT_EQ(both.out, "(standard input):3\nt1:1\n");
}

TEST_F(BuiltinCommandsTest, GrepMatchers) {
    MakeGrepFiles(root);
    EXPECT_EQ(RunCaptured("grep", {"-E", "two|ten", "/g/a.c"}).out, "two TODO\nten TODO\n");
    EXPECT_EQ(RunCaptured("grep", {"-F", "T.D", "/g/t1"}).status, 1);
    const Captured conflict = RunCaptured("grep", {"-E", "-F", "x", "/g/t1"});
    EXPECT_EQ(conflict.err, "grep: conflicting matchers specified\n");
    EXPECT_EQ(conflict.status, 2);
    const Captured perl = RunCaptured("grep", {"-P", "-e", "a", "-e", "b"});
    EXPECT_EQ(perl.err, "grep: the -P option only supports a single pattern\n");
    EXPECT_EQ(perl.status, 2);
    const Captured digits = RunCaptured("grep", {"-P", "\\d+"}, "x 42\n");
    EXPECT_EQ(digits.out, "x 42\n");
    EXPECT_EQ(digits.status, 0);
}

TEST_F(BuiltinCommandsTest, GrepSeveralPatterns) {
    MakeGrepFiles(root);
    const std::string expected = "two TODO\nten TODO\n";
    EXPECT_EQ(RunCaptured("grep", {"-e", "two", "-e", "ten", "/g/a.c"}).out, expected);
    // One pattern argument holding a newline: two patterns.
    EXPECT_EQ(RunCaptured("grep", {"two\nten", "/g/a.c"}).out, expected);
    EXPECT_EQ(RunCaptured("grep", {"-f", "/g/pat", "/g/a.c"}).out, expected);
    // An empty pattern file adds none: nothing ever matches.
    const Captured empty = RunCaptured("grep", {"-f", "/g/empty", "/g/t1"});
    EXPECT_EQ(empty.out, "");
    EXPECT_EQ(empty.status, 1);
    // -e '' is one empty pattern, which matches every line.
    EXPECT_EQ(RunCaptured("grep", {"-e", "", "-c", "/g/a.c"}).out, "10\n");
}

TEST_F(BuiltinCommandsTest, GrepCaseAndInvert) {
    MakeGrepFiles(root);
    EXPECT_EQ(RunCaptured("grep", {"-i", "todo", "/g/t1"}).out, "TODO\n");
    EXPECT_EQ(RunCaptured("grep", {"-y", "todo", "/g/t1"}).out, "TODO\n");
    EXPECT_EQ(RunCaptured("grep", {"-i", "--no-ignore-case", "todo", "/g/t1"}).status, 1);
    EXPECT_EQ(RunCaptured("grep", {"-vc", "TODO", "/g/a.c"}).out, "7\n");
}

TEST_F(BuiltinCommandsTest, GrepWordsAndLines) {
    MakeGrepFiles(root);
    EXPECT_EQ(RunCaptured("grep", {"-ob", "-w", "foo"}, "foo_bar foo\n").out, "8:foo\n");
    EXPECT_EQ(RunCaptured("grep", {"-o", "-w", "a*b"}, "aab ab\n").out, "aab\nab\n");
    EXPECT_EQ(RunCaptured("grep", {"-c", "-w", "-F", "-e", "foo", "-e", "foox"}, "xfoo foox\n").out,
        "1\n");
    EXPECT_EQ(RunCaptured("grep", {"-c", "-w", ""}, "a\n\n b\n").out, "2\n");
    EXPECT_EQ(RunCaptured("grep", {"-n", "-x", ""}, "a\n\n b\n").out, "2:\n");
    EXPECT_EQ(RunCaptured("grep", {"-x", "six", "/g/a.c"}).out, "six\n");
}

TEST_F(BuiltinCommandsTest, GrepOnlyMatching) {
    MakeGrepFiles(root);
    EXPECT_EQ(RunCaptured("grep", {"-o", "-e", "b", "-e", "bc", "-e", "a"}, "abc\n").out, "a\nbc\n");
    // Empty matches print nothing, but the line is still selected: exit 0.
    const Captured empty = RunCaptured("grep", {"-o", "x*", "/g/t1"});
    EXPECT_EQ(empty.out, "");
    EXPECT_EQ(empty.err, "");
    EXPECT_EQ(empty.status, 0);
}

TEST_F(BuiltinCommandsTest, GrepListModes) {
    MakeGrepFiles(root);
    EXPECT_EQ(RunCaptured("grep", {"-l", "TODO", "a.c", "b.h", "k"}, "", "/g").out, "a.c\nb.h\n");
    // GNU 3.5+: even -L exits by "a line selected", not "a file listed".
    const Captured nonMatch = RunCaptured("grep", {"-L", "TODO", "t1", "k"}, "", "/g");
    EXPECT_EQ(nonMatch.out, "k\n");
    EXPECT_EQ(nonMatch.status, 0);
    const Captured none = RunCaptured("grep", {"-L", "TODO", "t1"}, "", "/g");
    EXPECT_EQ(none.out, "");
    EXPECT_EQ(none.status, 0);
    // -l beats -c, whichever comes first.
    EXPECT_EQ(RunCaptured("grep", {"-lc", "TODO", "a.c"}, "", "/g").out, "a.c\n");
}

TEST_F(BuiltinCommandsTest, GrepMaxCount) {
    MakeGrepFiles(root);
    EXPECT_EQ(RunCaptured("grep", {"-m2", "-n", "TODO", "a.c"}, "", "/g").out,
        "2:two TODO\n5:five TODO\n");
    // -m 0: the input is not read at all.
    EXPECT_EQ(RunCaptured("grep", {"-m", "0", "TODO", "t1"}, "", "/g").status, 1);
    // A negative NUM is no limit.
    EXPECT_EQ(RunCaptured("grep", {"-m", "-1", "TODO", "t1"}, "", "/g").out, "TODO\n");
    const Captured invalid = RunCaptured("grep", {"-m", "x", "y", "t1"}, "", "/g");
    EXPECT_EQ(invalid.err, "grep: invalid max count\n");
    EXPECT_EQ(invalid.status, 2);
    EXPECT_EQ(RunCaptured("grep", {"-c", "-v", "-m2", "TODO", "a.c"}, "", "/g").out, "2\n");
}

TEST_F(BuiltinCommandsTest, GrepQuietStopsAtFirstMatch) {
    MakeGrepFiles(root);
    const Captured captured = RunCaptured("grep", {"-q", "TODO", "/g/a.c"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, GrepBinaryFiles) {
    MakeGrepFiles(root);
    // Only a NUL makes a file binary, and it says so on stderr.
    const Captured captured = RunCaptured("grep", {"TODO", "bin"}, "", "/g");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "grep: bin: binary file matches\n");
    EXPECT_EQ(captured.status, 0);
    // -s does not hide that one.
    const Captured quiet = RunCaptured("grep", {"-s", "TODO", "bin"}, "", "/g");
    EXPECT_EQ(quiet.err, "grep: bin: binary file matches\n");
    EXPECT_EQ(quiet.status, 0);
    // -c counts on, without the message.
    const Captured count = RunCaptured("grep", {"-c", "TODO", "bin"}, "", "/g");
    EXPECT_EQ(count.out, "1\n");
    EXPECT_EQ(count.err, "");
    EXPECT_EQ(count.status, 0);
    // -a: the NUL is just a byte of the line.
    EXPECT_EQ(RunCaptured("grep", {"-a", "-n", "y", "bin"}, "", "/g").out,
        std::string("1:x\0y TODO\n", 11));
    // Once binary, a NUL ends a line too: two lines here.
    EXPECT_EQ(RunCaptured("grep", {"-c", "a"}, std::string("a\0a\n", 4)).out, "2\n");
    // -I: the input has no selected line at all.
    const Captured ignore = RunCaptured("grep", {"-c", "-I", "x"}, std::string("x\0y\n", 4));
    EXPECT_EQ(ignore.out, "0\n");
    EXPECT_EQ(ignore.status, 1);
    // -z: NUL is the line terminator, so nothing is binary.
    EXPECT_EQ(RunCaptured("grep", {"-z", "-c", "x"}, std::string("x\0y\n", 4)).out, "1\n");
    const Captured badType = RunCaptured("grep", {"--binary-files=foo", "TODO", "/g/t1"});
    EXPECT_EQ(badType.err, "grep: unknown binary-files type\n");
    EXPECT_EQ(badType.status, 2);
}

TEST_F(BuiltinCommandsTest, GrepNullData) {
    MakeGrepFiles(root);
    const Captured captured = RunCaptured("grep", {"-z", "b"}, std::string("a\0b\0", 4));
    EXPECT_EQ(captured.out, std::string("b\0", 2));
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, GrepUsageErrors) {
    MakeGrepFiles(root);
    const std::string usage =
        "Usage: grep [OPTION]... PATTERNS [FILE]...\nTry 'grep --help' for more information.\n";
    const Captured none = RunCaptured("grep", {});
    EXPECT_EQ(none.err, usage);
    EXPECT_EQ(none.status, 2);
    const Captured shortOption = RunCaptured("grep", {"-k", "x"});
    EXPECT_EQ(shortOption.err, "grep: invalid option -- 'k'\n" + usage);
    EXPECT_EQ(shortOption.status, 2);
    const Captured longOption = RunCaptured("grep", {"--foo", "x"});
    EXPECT_EQ(longOption.err, "grep: unrecognized option '--foo'\n" + usage);
    EXPECT_EQ(longOption.status, 2);
}

TEST_F(BuiltinCommandsTest, GrepClassSyntaxError) {
    MakeGrepFiles(root);
    const std::string message = "grep: character class syntax is [[:space:]], not [:space:]\n";
    const Captured bare = RunCaptured("grep", {"[:space:]"});
    EXPECT_EQ(bare.err, message);
    EXPECT_EQ(bare.status, 2);
    const Captured extended = RunCaptured("grep", {"-E", "x[:a:]"});
    EXPECT_EQ(extended.err, message);
    EXPECT_EQ(extended.status, 2);
    // The proper spelling is fine (and does not match).
    EXPECT_EQ(RunCaptured("grep", {"[[:space:]]", "/g/t1"}).status, 1);
}

TEST_F(BuiltinCommandsTest, GrepBadRegex) {
    MakeGrepFiles(root);
    const Captured basic = RunCaptured("grep", {"a\\{1"});
    EXPECT_EQ(basic.err, "grep: Unmatched \\{\n");
    EXPECT_EQ(basic.status, 2);
    const Captured extended = RunCaptured("grep", {"-E", "("});
    EXPECT_EQ(extended.err, "grep: Unmatched ( or \\(\n");
    EXPECT_EQ(extended.status, 2);
}

TEST_F(BuiltinCommandsTest, GrepObsoleteU) {
    MakeGrepFiles(root);
    const Captured captured = RunCaptured("grep", {"-u", "x", "/g/t1"});
    EXPECT_EQ(captured.err, "grep: warning: --unix-byte-offsets (-u) is obsolete\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, EgrepAndFgrepRunAsGrepEAndF) {
    MakeGrepFiles(root);
    const Captured egrep = RunCaptured("egrep", {"T|x", "/g/t1"});
    EXPECT_EQ(egrep.out, "TODO\n");
    EXPECT_EQ(egrep.err, "");
    EXPECT_EQ(egrep.status, 0);
    const Captured fgrep = RunCaptured("fgrep", {"-c", "TO.O", "/g/t1"});
    EXPECT_EQ(fgrep.out, "0\n");
    EXPECT_EQ(fgrep.err, "");
    EXPECT_EQ(fgrep.status, 1);
    // No obsolescence warning, and every diagnostic names grep.
    const Captured conflict = RunCaptured("egrep", {"-F", "x", "/g/t1"});
    EXPECT_EQ(conflict.err, "grep: conflicting matchers specified\n");
    EXPECT_EQ(conflict.status, 2);
}

TEST_F(BuiltinCommandsTest, GrepAddsMissingNewline) {
    MakeGrepFiles(root);
    const Captured captured = RunCaptured("grep", {"TODO"}, "TODO");
    EXPECT_EQ(captured.out, "TODO\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, GrepRecursiveOptionsAreTreated) {
    MakeGrepTree(root);
    // -r and friends were reported not treated before search--grep-recursive;
    // now they are treated, and nothing reports them.
    const Captured captured = RunCaptured("grep",
        {"-r", "-n", "-m1", "--color=never", "--include=a*", "TODO", "src"}, "", "/g");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.out, "src/a.c:2:two TODO\n");
    EXPECT_EQ(captured.status, 0);
}

// --- grep: recursion, filters, context, colour, -Z ---

TEST_F(BuiltinCommandsTest, GrepRecursiveDefaultsToDot) {
    MakeGrepTree(root);
    // No operand with -r: . is searched, its names printed without the ./.
    const Captured captured = RunCaptured("grep", {"-rl", "TODO"}, "", "/g");
    EXPECT_EQ(captured.out, ".hid/h.c\nsrc/a.c\nsrc/bin.dat\nsrc/sub/b.h\nt1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    // An explicit . keeps the ./.
    const Captured dot = RunCaptured("grep", {"-rl", "TODO", "."}, "", "/g");
    EXPECT_EQ(dot.out, "./.hid/h.c\n./src/a.c\n./src/bin.dat\n./src/sub/b.h\n./t1\n");
    EXPECT_EQ(dot.status, 0);
}

TEST_F(BuiltinCommandsTest, GrepRecursivePrefixes) {
    MakeGrepTree(root);
    // A child's name: the operand with any run of trailing / cut to one.
    EXPECT_EQ(RunCaptured("grep", {"-rl", "TODO sub", "src//"}, "", "/g").out, "src/sub/b.h\n");
    EXPECT_EQ(RunCaptured("grep", {"-rl", "TODO sub", ".//src"}, "", "/g").out, ".//src/sub/b.h\n");
    // A single operand that is a file prints no name.
    EXPECT_EQ(RunCaptured("grep", {"-r", "ten", "src/a.c"}, "", "/g").out, "ten TODO\n");
    EXPECT_EQ(RunCaptured("grep", {"-rh", "TODO sub", "src"}, "", "/g").out, "TODO sub\n");
}

TEST_F(BuiltinCommandsTest, GrepRecursiveLineNumbers) {
    MakeGrepTree(root);
    const Captured captured = RunCaptured("grep", {"-rn", "TODO", "src"}, "", "/g");
    EXPECT_EQ(captured.out,
        "src/a.c:2:two TODO\n"
        "src/a.c:5:five TODO\n"
        "src/a.c:10:ten TODO\n"
        "src/sub/b.h:1:TODO sub\n");
    // The binary file says so between a.c's and b.h's lines.
    EXPECT_EQ(captured.err, "grep: src/bin.dat: binary file matches\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, GrepIncludeExclude) {
    MakeGrepTree(root);
    // Only files whose base name matches the --include.
    const Captured include = RunCaptured("grep", {"-rl", "--include=*.c", "TODO", "src"}, "", "/g");
    EXPECT_EQ(include.out, "src/a.c\n");
    EXPECT_EQ(include.status, 0);
    // The list is walked from the last given to the first, the first match
    // deciding; none matching keeps the first given's side.
    const Captured excludeInclude =
        RunCaptured("grep", {"-rl", "--exclude=a*", "--include=*.h", "TODO", "src"}, "", "/g");
    EXPECT_EQ(excludeInclude.out, "src/bin.dat\nsrc/sub/b.h\n");
    EXPECT_EQ(excludeInclude.status, 0);
    const Captured includeExclude =
        RunCaptured("grep", {"-rl", "--include=*.h", "--exclude=b*", "TODO", "src"}, "", "/g");
    EXPECT_EQ(includeExclude.out, "");
    EXPECT_EQ(includeExclude.status, 1);
    // Operands are filtered too, unanchored.
    const Captured operands =
        RunCaptured("grep", {"-l", "--include=*.h", "TODO", "src/a.c", "t1"}, "", "/g");
    EXPECT_EQ(operands.out, "");
    EXPECT_EQ(operands.status, 1);
    // --exclude-dir applies to an operand directory too.
    const Captured dir =
        RunCaptured("grep", {"-rl", "--exclude-dir=sub", "TODO", "src/sub"}, "", "/g");
    EXPECT_EQ(dir.out, "");
    EXPECT_EQ(dir.status, 1);
}

TEST_F(BuiltinCommandsTest, GrepExcludeFrom) {
    MakeGrepTree(root);
    WriteTo(root, "/g/f", "a*\n*.dat\n");
    const Captured captured = RunCaptured("grep", {"-rl", "--exclude-from=f", "TODO", "src"}, "", "/g");
    EXPECT_EQ(captured.out, "src/sub/b.h\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    // A missing file is trouble, not an empty list.
    const Captured missing = RunCaptured("grep", {"--exclude-from=nosuch", "TODO", "t1"}, "", "/g");
    EXPECT_EQ(missing.err, "grep: nosuch: No such file or directory\n");
    EXPECT_EQ(missing.status, 2);
}

TEST_F(BuiltinCommandsTest, GrepDirectoriesOption) {
    MakeGrepTree(root);
    const Captured skip = RunCaptured("grep", {"-d", "skip", "TODO", "src"}, "", "/g");
    EXPECT_EQ(skip.out, "");
    EXPECT_EQ(skip.status, 1);
    EXPECT_EQ(RunCaptured("grep", {"-d", "recurse", "-l", "TODO sub", "src"}, "", "/g").out,
        "src/sub/b.h\n");
    const std::string valid =
        "Valid arguments are:\n"
        "  - 'read'\n"
        "  - 'recurse'\n"
        "  - 'skip'\n"
        "Usage: grep [OPTION]... PATTERNS [FILE]...\n"
        "Try 'grep --help' for more information.\n";
    const Captured invalid = RunCaptured("grep", {"-d", "foo", "x", "t1"}, "", "/g");
    EXPECT_EQ(invalid.err, "grep: invalid argument 'foo' for '--directories'\n" + valid);
    EXPECT_EQ(invalid.status, 1);
    const Captured ambiguous = RunCaptured("grep", {"-d", "r", "x", "t1"}, "", "/g");
    EXPECT_EQ(ambiguous.err, "grep: ambiguous argument 'r' for '--directories'\n" + valid);
    EXPECT_EQ(ambiguous.status, 1);
    const Captured devices = RunCaptured("grep", {"-D", "foo", "x", "t1"}, "", "/g");
    EXPECT_EQ(devices.err, "grep: unknown devices method\n");
    EXPECT_EQ(devices.status, 2);
}

TEST_F(BuiltinCommandsTest, GrepDevicesSkippedWhileRecursing) {
    MakeGrepTree(root);
    ASSERT_EQ(root->CreateDirectory("/g/dev", kDirMode), 0);
    root->Mount("/g/dev",
        factory->CreateServicesCreator()->CreateFileSystemService()->CreateDeviceFileSystem());
    // Devices met while recursing are skipped, zero's endless bytes never read.
    const Captured walk = RunCaptured("grep", {"-rl", "x", "dev"}, "", "/g");
    EXPECT_EQ(walk.out, "");
    EXPECT_EQ(walk.status, 1);
    // A device operand is read (GNU's -D default).
    const Captured operand = RunCaptured("grep", {"-c", "x", "dev/null"}, "", "/g");
    EXPECT_EQ(operand.out, "0\n");
    EXPECT_EQ(operand.status, 1);
    const Captured skipped = RunCaptured("grep", {"-D", "skip", "-c", "x", "dev/null"}, "", "/g");
    EXPECT_EQ(skipped.out, "");
    EXPECT_EQ(skipped.status, 1);
}

TEST_F(BuiltinCommandsTest, GrepContextLines) {
    MakeGrepTree(root);
    EXPECT_EQ(RunCaptured("grep", {"-n", "-B1", "-A1", "two\\|five", "src/a.c"}, "", "/g").out,
        "1-one\n2:two TODO\n3-three\n4-four\n5:five TODO\n6-six\n");
    EXPECT_EQ(RunCaptured("grep", {"-2", "-n", "five", "src/a.c"}, "", "/g").out,
        "3-three\n4-four\n5:five TODO\n6-six\n7-seven\n");
    // -A0: context asked for, so the separators between the groups stay.
    EXPECT_EQ(RunCaptured("grep", {"-A0", "TODO", "src/a.c"}, "", "/g").out,
        "two TODO\n--\nfive TODO\n--\nten TODO\n");
    EXPECT_EQ(RunCaptured("grep", {"-n", "-A1", "--group-separator=XX", "two\\|ten", "src/a.c"}, "", "/g").out,
        "2:two TODO\n3-three\nXX\n10:ten TODO\n");
    EXPECT_EQ(RunCaptured("grep", {"-n", "-A1", "--no-group-separator", "two\\|ten", "src/a.c"}, "", "/g").out,
        "2:two TODO\n3-three\n10:ten TODO\n");
    // A separator also goes between groups of different files.
    EXPECT_EQ(RunCaptured("grep", {"-n", "-C1", "TODO", "src/a.c", "src/sub/b.h"}, "", "/g").out,
        "src/a.c-1-one\nsrc/a.c:2:two TODO\nsrc/a.c-3-three\nsrc/a.c-4-four\n"
        "src/a.c:5:five TODO\nsrc/a.c-6-six\n"
        "--\n"
        "src/a.c-9-nine\nsrc/a.c:10:ten TODO\n"
        "--\n"
        "src/sub/b.h:1:TODO sub\n");
    const Captured invalid = RunCaptured("grep", {"-A", "x", "y", "t1"}, "", "/g");
    EXPECT_EQ(invalid.err, "grep: x: invalid context length argument\n");
    EXPECT_EQ(invalid.status, 2);
}

TEST_F(BuiltinCommandsTest, GrepContextWithMaxCountAndOnly) {
    MakeGrepTree(root);
    // -m1: the trailing context is still read, a matching line in it printed
    // as a match but restarting nothing.
    EXPECT_EQ(RunCaptured("grep", {"-n", "-m1", "-A4", "TODO", "src/a.c"}, "", "/g").out,
        "2:two TODO\n3-three\n4-four\n5:five TODO\n6-six\n");
    // -o prints no context lines, but the separators stay.
    EXPECT_EQ(RunCaptured("grep", {"-o", "-n", "-A1", "TODO", "src/a.c"}, "", "/g").out,
        "2:TODO\n--\n5:TODO\n--\n10:TODO\n");
    // -c has no context at all.
    EXPECT_EQ(RunCaptured("grep", {"-C1", "-c", "TODO", "src/a.c"}, "", "/g").out, "3\n");
}

TEST_F(BuiltinCommandsTest, GrepNullAfterNames) {
    MakeGrepTree(root);
    const Captured list = RunCaptured("grep", {"-lZ", "TODO", "src/a.c", "src/sub/b.h"}, "", "/g");
    EXPECT_EQ(list.out, std::string("src/a.c") + '\0' + "src/sub/b.h" + '\0');
    EXPECT_EQ(list.status, 0);
    const Captured count = RunCaptured("grep", {"-Z", "-c", "TODO", "src/a.c", "src/sub/b.h"}, "", "/g");
    EXPECT_EQ(count.out, std::string("src/a.c") + '\0' + "3\n" + "src/sub/b.h" + '\0' + "1\n");
    // One file: no name, so no NUL either.
    EXPECT_EQ(RunCaptured("grep", {"-Z", "-n", "TODO", "src/sub/b.h"}, "", "/g").out,
        "1:TODO sub\n");
}

TEST_F(BuiltinCommandsTest, GrepColorAlways) {
    MakeGrepFiles(root);
    const std::string esc = "\x1b";
    // One coloured piece: ESC [ V m ESC [ K, the text, ESC [ m ESC [ K.
    const auto piece = [&esc](const std::string& value, const std::string& text) {
        return esc + "[" + value + "m" + esc + "[K" + text + esc + "[m" + esc + "[K";
    };
    const std::string match = piece("01;31", "TODO");
    // -n with two files: the name (fn), the separators (se), the line number
    // (ln), the match (ms); the line itself adds nothing (sl: the default).
    EXPECT_EQ(RunCaptured("grep", {"--color=always", "-n", "TODO", "a.c", "b.h"}, "", "/g").out,
        piece("35", "a.c") + piece("36", ":") + piece("32", "2") + piece("36", ":") + "two " + match + "\n"
        + piece("35", "a.c") + piece("36", ":") + piece("32", "5") + piece("36", ":") + "five " + match + "\n"
        + piece("35", "a.c") + piece("36", ":") + piece("32", "10") + piece("36", ":") + "ten " + match + "\n"
        + piece("35", "b.h") + piece("36", ":") + piece("32", "1") + piece("36", ":") + match + " sub\n");
    // A context line: '-' separators, the line in cx (nothing by default).
    // The pattern is "five", so that is the highlighted match.
    EXPECT_EQ(RunCaptured("grep", {"--color=always", "-n", "-H", "-C1", "five", "a.c"}, "", "/g").out,
        piece("35", "a.c") + piece("36", "-") + piece("32", "4") + piece("36", "-") + "four\n"
        + piece("35", "a.c") + piece("36", ":") + piece("32", "5") + piece("36", ":") + piece("01;31", "five") + " TODO\n"
        + piece("35", "a.c") + piece("36", "-") + piece("32", "6") + piece("36", "-") + "six\n");
    // -c and -l colour the name (and -c's separator); with one operand the
    // name needs -H to be shown at all.
    EXPECT_EQ(RunCaptured("grep", {"--color=always", "-c", "-H", "TODO", "a.c"}, "", "/g").out,
        piece("35", "a.c") + piece("36", ":") + "3\n");
    EXPECT_EQ(RunCaptured("grep", {"--color=always", "-l", "-H", "TODO", "a.c"}, "", "/g").out,
        piece("35", "a.c") + "\n");
    // -o: the byte offset (bn), the separator, the match in ms.
    EXPECT_EQ(RunCaptured("grep", {"--color=always", "-o", "-b", "TODO", "a.c"}, "", "/g").out,
        piece("32", "8") + piece("36", ":") + match + "\n"
        + piece("32", "29") + piece("36", ":") + match + "\n"
        + piece("32", "59") + piece("36", ":") + match + "\n");
    // -v: the selected lines hold no matches, so only the prefix is coloured.
    EXPECT_EQ(RunCaptured("grep", {"--color=always", "-v", "-n", "TODO", "a.c"}, "", "/g").out,
        piece("32", "1") + piece("36", ":") + "one\n"
        + piece("32", "3") + piece("36", ":") + "three\n"
        + piece("32", "4") + piece("36", ":") + "four\n"
        + piece("32", "6") + piece("36", ":") + "six\n"
        + piece("32", "7") + piece("36", ":") + "seven\n"
        + piece("32", "8") + piece("36", ":") + "eight\n"
        + piece("32", "9") + piece("36", ":") + "nine\n");
    // -Z: the NUL after a name is written bare, never coloured.
    EXPECT_EQ(RunCaptured("grep", {"-Z", "-n", "--color=always", "TODO", "t1", "b.h"}, "", "/g").out,
        piece("35", "t1") + std::string(1, '\0') + piece("32", "1") + piece("36", ":") + match + "\n"
        + piece("35", "b.h") + std::string(1, '\0') + piece("32", "1") + piece("36", ":") + match + " sub\n");
    // sl colours the line around the highlighted match, restarted after it.
    auto environment = os->GetOsEnvironment()->Clone();
    environment->SetVariable("GREP_COLORS", "sl=1:cx=2");
    EXPECT_EQ(RunCaptured("grep", {"--color=always", "-A1", "T"}, "TODO\n", "/", environment).out,
        esc + "[1m" + esc + "[K" + piece("01;31", "T") + esc + "[1m" + esc + "[K" + "ODO" + esc + "[m" + esc + "[K\n");
}

TEST_F(BuiltinCommandsTest, GrepColorAutoAndNever) {
    MakeGrepFiles(root);
    const std::string esc = "\x1b";
    // RunCaptured's streams are files, not terminals: auto colours nothing.
    EXPECT_EQ(RunCaptured("grep", {"--color=auto", "TODO", "t1"}, "", "/g").out, "TODO\n");
    EXPECT_EQ(RunCaptured("grep", {"--colour=auto", "TODO", "t1"}, "", "/g").out, "TODO\n");
    EXPECT_EQ(RunCaptured("grep", {"--color", "TODO", "t1"}, "", "/g").out, "TODO\n");
    // The console's output is a terminal: auto colours when TERM is usable.
    os->GetOsEnvironment()->SetVariable("TERM", "xterm");
    int status = -1;
    const Lines coloured = Run("grep", {"--color=auto", "TODO", "/g/t1"}, &status);
    EXPECT_EQ(status, 0);
    std::string text;
    for (const auto& line : coloured) {
        text += line + "\n";
    }
    EXPECT_EQ(text, esc + "[01;31m" + esc + "[KTODO" + esc + "[m" + esc + "[K\n");
    // A dumb terminal gets no colour, but an explicit ALWAYS does.
    os->GetOsEnvironment()->SetVariable("TERM", "dumb");
    EXPECT_EQ(Run("grep", {"--color=auto", "TODO", "/g/t1"}), Lines{"TODO"});
    const Lines forced = Run("grep", {"--color=ALWAYS", "TODO", "/g/t1"});
    std::string forcedText;
    for (const auto& line : forced) {
        forcedText += line + "\n";
    }
    EXPECT_EQ(forcedText, esc + "[01;31m" + esc + "[KTODO" + esc + "[m" + esc + "[K\n");
    // A WHEN GNU does not know: the full --help on stdout, exit 0.
    const Captured help = RunCaptured("grep", {"--help"});
    const Captured bad = RunCaptured("grep", {"--colour=bad", "x", "t1"}, "", "/g");
    EXPECT_EQ(bad.out, help.out);
    EXPECT_EQ(bad.status, 0);
}

TEST_F(BuiltinCommandsTest, GrepPerlWordWrap) {
    MakeGrepFiles(root);
    // -P -w gets the same word-neighbour check as the other matchers (what
    // GNU's (?<!\w)...(?!\w) wrap means), not the \b wrap: a pattern starting
    // with a non-word byte matches at one.
    const Captured flag = RunCaptured("grep", {"-Pw", "--", "-b"}, "a -b\n");
    EXPECT_EQ(flag.out, "a -b\n");
    EXPECT_EQ(flag.status, 0);
    // -x and -w combine as GNU's: the line must match the pattern as a whole
    // and be a whole word.
    const Captured both = RunCaptured("grep", {"-Pxw", "--", "-b"}, "a -b\n");
    EXPECT_EQ(both.out, "");
    EXPECT_EQ(both.status, 1);
    const Captured trailing = RunCaptured("grep", {"-Pw", "b-"}, "a b-\n");
    EXPECT_EQ(trailing.out, "a b-\n");
    EXPECT_EQ(trailing.status, 0);
}

TEST_F(BuiltinCommandsTest, GrepColorEnvironment) {
    MakeGrepFiles(root);
    const std::string esc = "\x1b";
    const auto withEnv = [&](const std::string& name, const std::string& value) {
        auto environment = os->GetOsEnvironment()->Clone();
        environment->SetVariable(name, value);
        return RunCaptured("grep", {"--color=always", "TODO"}, "TODO\n", "/", environment);
    };
    // The deprecated GREP_COLOR (digits and ';' only) sets ms and mc, with
    // the warning when GREP_COLORS does not replace it.
    const Captured grepColor = withEnv("GREP_COLOR", "01;32");
    EXPECT_EQ(grepColor.out, esc + "[01;32m" + esc + "[KTODO" + esc + "[m" + esc + "[K\n");
    EXPECT_EQ(grepColor.err,
        "grep: warning: GREP_COLOR='01;32' is deprecated; use GREP_COLORS='mt=01;32'\n");
    // ne drops both ESC [ K bytes; mt sets ms and mc together.
    const Captured ne = withEnv("GREP_COLORS", "ne");
    EXPECT_EQ(ne.out, esc + "[01;31mTODO" + esc + "[m\n");
    EXPECT_EQ(ne.err, "");
    const Captured mt = withEnv("GREP_COLORS", "mt=01;34");
    EXPECT_EQ(mt.out, esc + "[01;34m" + esc + "[KTODO" + esc + "[m" + esc + "[K\n");
    EXPECT_EQ(mt.err, "");
    // Parsing stops at the first malformed entry, keeping what was read.
    const Captured partial = withEnv("GREP_COLORS", "ms=1:junk");
    EXPECT_EQ(partial.out, esc + "[1m" + esc + "[KTODO" + esc + "[m" + esc + "[K\n");
    EXPECT_EQ(partial.err, "");
}

// --- GrepMatcher ---

TEST(GrepMatcherTest, FixedLeftmostLongest) {
    std::string error;
    GrepMatcherOptions options;
    options.syntax = GrepSyntax::Fixed;
    const auto matcher = GrepMatcher::Create({"b", "bc", "a"}, options, error);
    ASSERT_NE(matcher, nullptr) << error;
    size_t begin = 0;
    size_t end = 0;
    ASSERT_TRUE(matcher->Find("abc", 0, begin, end));
    EXPECT_EQ(begin, 0u);
    EXPECT_EQ(end, 1u);
    ASSERT_TRUE(matcher->Find("abc", 1, begin, end));
    EXPECT_EQ(begin, 1u);
    EXPECT_EQ(end, 3u);
}

TEST(GrepMatcherTest, BasicJoinedAndSeparate) {
    std::string error;
    GrepMatcherOptions options;
    options.syntax = GrepSyntax::Basic;
    // Without a back-reference: one joined regex.
    const auto joined = GrepMatcher::Create({"ab", "ax"}, options, error);
    ASSERT_NE(joined, nullptr) << error;
    // With one: separate regexes.
    const auto separate = GrepMatcher::Create({"a\\(b\\)", "a\\(x\\)"}, options, error);
    ASSERT_NE(separate, nullptr) << error;
    for (const auto& matcher : {joined.get(), separate.get()}) {
        SCOPED_TRACE("joined vs separate");
        size_t begin = 0;
        size_t end = 0;
        ASSERT_TRUE(matcher->Find("xab", 0, begin, end));
        EXPECT_EQ(begin, 1u);
        EXPECT_EQ(end, 3u);
        ASSERT_TRUE(matcher->Find("xax", 0, begin, end));
        EXPECT_EQ(begin, 1u);
        EXPECT_EQ(end, 3u);
    }
}

TEST(GrepMatcherTest, WordShrinks) {
    std::string error;
    GrepMatcherOptions options;
    options.syntax = GrepSyntax::Basic;
    options.wholeWord = GrepWholeWord::NonWordNeighbours;
    const auto matcher = GrepMatcher::Create({"a*b"}, options, error);
    ASSERT_NE(matcher, nullptr) << error;
    size_t begin = 0;
    size_t end = 0;
    ASSERT_TRUE(matcher->Find("aab ab", 0, begin, end));
    EXPECT_EQ(begin, 0u);
    EXPECT_EQ(end, 3u);
    ASSERT_TRUE(matcher->Find("aab ab", 3, begin, end));
    EXPECT_EQ(begin, 4u);
    EXPECT_EQ(end, 6u);
    // Every match of "aabb" has a word byte before or after it, however
    // far the shrink-and-move-on loop reaches: no whole word.
    EXPECT_FALSE(matcher->Find("aabb", 0, begin, end));
}

TEST(GrepMatcherTest, WholeLinePerl) {
    std::string error;
    GrepMatcherOptions options;
    options.syntax = GrepSyntax::Perl;
    options.wholeLine = true;
    const auto matcher = GrepMatcher::Create({"a|ab"}, options, error);
    ASSERT_NE(matcher, nullptr) << error;
    size_t begin = 0;
    size_t end = 0;
    ASSERT_TRUE(matcher->Find("ab", 0, begin, end));
    EXPECT_EQ(begin, 0u);
    EXPECT_EQ(end, 2u);
}