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
    const Captured stdin = RunCaptured("grep", {"-Tn", "TODO"}, "TODO\n");
    EXPECT_EQ(stdin.out, std::string(18, ' ') + "1:\tTODO\n");
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

TEST_F(BuiltinCommandsTest, GrepReportsNotTreatedOptions) {
    MakeGrepFiles(root);
    // -r arrives with search--grep-recursive; until then it is reported and
    // the rest of the command works.
    const Captured captured = RunCaptured("grep", {"-r", "TODO", "/g/t1"});
    EXPECT_EQ(captured.err, "Parameter -r is not treated by HaisosOS grep v. 1.0.0\n");
    EXPECT_EQ(captured.out, "TODO\n");
    EXPECT_EQ(captured.status, 0);
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