#include <gtest/gtest.h>
#include <memory>
#include <regex>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "commands/diff/DiffEngine.h"

namespace Haisos {
namespace {

// The files every diff test compares; each expectation below was observed
// from GNU diffutils 3.10 under LC_ALL=C TZ=UTC on these very contents.
void MakeDiffFiles(const std::shared_ptr<IFileSystem>& fs) {
    auto write = [&](const std::string& path, const std::string& content) {
        auto file = fs->OpenFile(path, kFileOpenWriteCreateTruncate, kFileCreateMode);
        ASSERT_NE(file, nullptr) << path;
        file->Write(content.data(), content.size());
    };
    write("/a", "a\nb\nc\n");
    write("/b", "a\nB\nc\nd");       // no newline at the end
    write("/c", "a\nb\nc\n");
    write("/n1", "a");               // no newline
    write("/n2", "b");
    write("/w1", "a b\nc\n");
    write("/w2", "a  b\nc\n");
    write("/sp1", "a b\nc\n");
    write("/sp2", "ab\nc\n");
    write("/i1", "A\nc\n");
    write("/i2", "a\nc\n");
    write("/B1", "a\n\n\nb\n");
    write("/B2", "a\nb\n");
    write("/T1", "a\tb\nc\n");
    write("/T2", "a\tX\nc\n");
    write("/E1", "a\n\n");
    write("/E2", "a\n \n");
    write("/CR1", "a\r\n");
    write("/CR2", "a\n");
    write("/bin1", std::string("x\0y\n", 5));
    write("/bin2", std::string("x\0z\n", 5));
    write("/bin3", std::string("x\0y\n", 5));
    write("/s1", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n");
    write("/s2", "1\n2\n3\nX\n5\n6\n7\n8\n9\n10\n");
    write("/d1", "a\nb\nc\n");
    write("/d2", "a\nc\n");
}

// The body of an output: everything from |marker| on, dropping the file
// header lines whose times change from run to run.
std::string BodyFrom(const std::string& out, const std::string& marker) {
    const size_t at = out.find(marker);
    return at == std::string::npos ? out : out.substr(at);
}

} // namespace

TEST_F(BuiltinCommandsTest, DiffNormalChangeAndMissingNewline) {
    MakeDiffFiles(root);
    const auto captured = RunCaptured("diff", {"a", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "2c2\n"
              "< b\n"
              "---\n"
              "> B\n"
              "3a4\n"
              "> d\n"
              "\\ No newline at end of file\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, DiffIdenticalIsSilent) {
    MakeDiffFiles(root);
    auto captured = RunCaptured("diff", {"a", "c"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // -s says it: the operand names, unquoted, as given.
    captured = RunCaptured("diff", {"-s", "a", "c"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "Files a and c are identical\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, DiffBriefAndDeleteInsert) {
    MakeDiffFiles(root);
    auto captured = RunCaptured("diff", {"-q", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "Files a and b differ\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("diff", {"-q", "a", "c"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    // A pure delete, and the same pair the other way round: a pure insert.
    captured = RunCaptured("diff", {"d1", "d2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "2d1\n< b\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("diff", {"d2", "d1"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1a2\n> b\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, DiffUnifiedWithLabels) {
    MakeDiffFiles(root);
    // Two labels replace both header lines whole: no tabs, no times.
    auto captured = RunCaptured("diff", {"-u", "-L", "one", "-L", "two", "a", "b"},
                                std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "--- one\n"
              "+++ two\n"
              "@@ -1,3 +1,4 @@\n"
              " a\n"
              "-b\n"
              "+B\n"
              " c\n"
              "+d\n"
              "\\ No newline at end of file\n");
    EXPECT_EQ(captured.status, 1);

    // One label names the first file alone; the second keeps its time.
    captured = RunCaptured("diff", {"-u", "-L", "one", "a", "b"}, std::nullopt, "/");
    const std::string out = captured.out;
    EXPECT_NE(out.find("--- one\n"), std::string::npos);
    EXPECT_NE(out.find("+++ b\t"), std::string::npos);
    EXPECT_EQ(BodyFrom(out, "@@ -1,3 +1,4 @@\n"),
              "@@ -1,3 +1,4 @@\n"
              " a\n"
              "-b\n"
              "+B\n"
              " c\n"
              "+d\n"
              "\\ No newline at end of file\n");
}

TEST_F(BuiltinCommandsTest, DiffUnifiedZeroContextRanges) {
    MakeDiffFiles(root);
    const auto captured = RunCaptured("diff", {"-U0", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(BodyFrom(captured.out, "@@"),
              "@@ -2 +2 @@\n"
              "-b\n"
              "+B\n"
              "@@ -3,0 +4 @@\n"
              "+d\n"
              "\\ No newline at end of file\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, DiffUnifiedHeaderTimes) {
    MakeDiffFiles(root);
    const auto captured = RunCaptured("diff", {"-u", "s1", "s2"}, std::nullopt, "/");
    // The header times are the files' own: ISO format with nanoseconds and
    // a numeric zone, exactly as GNU prints them.
    const std::regex header(
        "^--- s1\t[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{9}"
        " [+-][0-9]{4}\n"
        "\\+\\+\\+ s2\t[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{9}"
        " [+-][0-9]{4}\n");
    EXPECT_TRUE(std::regex_search(captured.out, header)) << captured.out;
    EXPECT_EQ(BodyFrom(captured.out, "@@"),
              "@@ -1,7 +1,7 @@\n"
              " 1\n"
              " 2\n"
              " 3\n"
              "-4\n"
              "+X\n"
              " 5\n"
              " 6\n"
              " 7\n");
}

TEST_F(BuiltinCommandsTest, DiffContextFormat) {
    MakeDiffFiles(root);
    auto captured = RunCaptured("diff", {"-c", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(BodyFrom(captured.out, "***************"),
              "***************\n"
              "*** 1,3 ****\n"
              "  a\n"
              "! b\n"
              "  c\n"
              "--- 1,4 ----\n"
              "  a\n"
              "! B\n"
              "  c\n"
              "+ d\n"
              "\\ No newline at end of file\n");
    EXPECT_EQ(captured.status, 1);

    // -C0 shrinks each range to the changed lines alone.
    captured = RunCaptured("diff", {"-C0", "s1", "s2"}, std::nullopt, "/");
    EXPECT_EQ(BodyFrom(captured.out, "***************"),
              "***************\n"
              "*** 4 ****\n"
              "! 4\n"
              "--- 4 ----\n"
              "! X\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, DiffEdScript) {
    MakeDiffFiles(root);
    // The ed script lists the changes last-first; a file missing its final
    // newline is warned of after the script, and the exit status is 2.
    auto captured = RunCaptured("diff", {"-e", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "3a\n"
              "d\n"
              ".\n"
              "2c\n"
              "B\n"
              ".\n");
    EXPECT_EQ(captured.err, "diff: b: No newline at end of file\n\n");
    EXPECT_EQ(captured.status, 2);

    // Both files without a newline: two warnings, still no identical message.
    captured = RunCaptured("diff", {"-e", "n1", "n2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "1c\n"
              "b\n"
              ".\n");
    EXPECT_EQ(captured.err,
              "diff: n1: No newline at end of file\n"
              "\n"
              "diff: n2: No newline at end of file\n"
              "\n");
    EXPECT_EQ(captured.status, 2);

    // -q keeps only the one-line report: no script, no warnings.
    captured = RunCaptured("diff", {"-q", "-e", "n1", "n2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "Files n1 and n2 differ\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("diff", {"-q", "-e", "n1", "n1"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, DiffWhiteSpaceOptions) {
    MakeDiffFiles(root);
    // A change in the amount of space: -b, -E and -w all call it even.
    auto captured = RunCaptured("diff", {"-b", "w1", "w2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("diff", {"w1", "w2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1c1\n< a b\n---\n> a  b\n");
    EXPECT_EQ(captured.status, 1);

    // A space against none: -b still sees it, -w does not.
    captured = RunCaptured("diff", {"-b", "sp1", "sp2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1c1\n< a b\n---\n> ab\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("diff", {"-w", "sp1", "sp2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    // --strip-trailing-cr takes the CR off before comparing.
    captured = RunCaptured("diff", {"--strip-trailing-cr", "CR1", "CR2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    // -i folds case.
    captured = RunCaptured("diff", {"-i", "i1", "i2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("diff", {"i1", "i2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1c1\n< A\n---\n> a\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, DiffIgnoreBlankLines) {
    MakeDiffFiles(root);
    // All the changes are runs of blank lines: -B calls the files equal.
    auto captured = RunCaptured("diff", {"-B", "B1", "B2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("diff", {"-B", "-u", "B1", "B2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    // Without -B the blank run is a change like any other.
    captured = RunCaptured("diff", {"B1", "B2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "2,3d1\n< \n< \n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, DiffTabsAndMarks) {
    MakeDiffFiles(root);
    // -T puts a tab between the mark and the text.
    auto captured = RunCaptured("diff", {"-T", "T1", "T2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1c1\n<\ta\tb\n---\n>\ta\tX\n");
    EXPECT_EQ(captured.status, 1);

    // -t expands each tab to the next multiple of the tab size.
    captured = RunCaptured("diff", {"-t", "T1", "T2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1c1\n< a       b\n---\n> a       X\n");
    EXPECT_EQ(captured.status, 1);

    // --suppress-blank-empty drops the blank after a mark on an empty line.
    captured = RunCaptured("diff", {"--suppress-blank-empty", "-u", "E1", "E2"},
                           std::nullopt, "/");
    EXPECT_EQ(BodyFrom(captured.out, "@@"),
              "@@ -1,2 +1,2 @@\n"
              " a\n"
              "-\n"
              "+ \n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, DiffStdinAndBinary) {
    MakeDiffFiles(root);
    // "-" is the standard input: the same bytes as the file compare equal.
    auto captured = RunCaptured("diff", {"-", "a"}, "a\nb\nc\n", "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("diff", {"-", "b"}, "a\nX\nc\n", "/");
    EXPECT_EQ(captured.out,
              "2c2\n"
              "< X\n"
              "---\n"
              "> B\n"
              "3a4\n"
              "> d\n"
              "\\ No newline at end of file\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, DiffBinaryFiles) {
    MakeDiffFiles(root);
    // A NUL in the first 4096 bytes makes a file binary.
    auto captured = RunCaptured("diff", {"bin1", "bin2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "Binary files bin1 and bin2 differ\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);

    // -q reports the same difference without the "Binary" word.
    captured = RunCaptured("diff", {"-q", "bin1", "bin2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "Files bin1 and bin2 differ\n");
    EXPECT_EQ(captured.status, 1);

    // Binary and equal: only -s says anything.
    captured = RunCaptured("diff", {"bin1", "bin3"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("diff", {"-s", "bin1", "bin3"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "Files bin1 and bin3 are identical\n");
    EXPECT_EQ(captured.status, 0);

    // -a compares them as text, NUL bytes and all.
    captured = RunCaptured("diff", {"-a", "bin1", "bin2"}, std::nullopt, "/");
    const std::string expected = std::string("1c1\n< x") + '\0' + "y\n---\n> x" + '\0' + "z\n";
    EXPECT_EQ(captured.out, expected);
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, DiffErrors) {
    MakeDiffFiles(root);
    const std::string tryHelp = "diff: Try 'diff --help' for more information.\n";

    auto captured = RunCaptured("diff", {"a"}, std::nullopt, "/");
    EXPECT_EQ(captured.err, "diff: missing operand after 'a'\n" + tryHelp);
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("diff", {"a", "b", "c"}, std::nullopt, "/");
    EXPECT_EQ(captured.err, "diff: extra operand 'c'\n" + tryHelp);
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("diff", {"-u", "-c", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.err, "diff: conflicting output style options\n" + tryHelp);
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("diff", {"--no-such", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.err, "diff: unrecognized option '--no-such'\n" + tryHelp);
    EXPECT_EQ(captured.status, 2);

    // Both unopenable operands are reported, with no Try line.
    captured = RunCaptured("diff", {"missing1", "missing2"}, std::nullopt, "/");
    EXPECT_EQ(captured.err,
              "diff: missing1: No such file or directory\n"
              "diff: missing2: No such file or directory\n");
    EXPECT_EQ(captured.status, 2);
}

TEST_F(BuiltinCommandsTest, DiffHelpAndVersion) {
    MakeDiffFiles(root);
    const auto help = RunCaptured("diff", {"--help"}, std::nullopt, "/");
    EXPECT_EQ(help.status, 0);
    EXPECT_NE(help.out.find("HaisosOS diff version 1.0.0"), std::string::npos);
    EXPECT_NE(help.out.find("Not treated arguments:"), std::string::npos);

    const auto version = RunCaptured("diff", {"--version"}, std::nullopt, "/");
    EXPECT_EQ(version.out, "diff (HaisosOS builtin) 1.0.0\n");
    EXPECT_EQ(version.status, 0);

    // A not treated option is parsed, reported and gone past.
    const auto captured = RunCaptured("diff", {"-l", "a", "c"}, std::nullopt, "/");
    EXPECT_EQ(captured.err, "Parameter -l is not treated by HaisosOS diff v. 1.0.0\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);
}

namespace {

DiffText TextOf(const std::string& bytes) {
    return MakeDiffText(bytes, false, true);
}

// One line's bytes, the newline included.
std::string LineTextOf(const DiffText& text, size_t line) {
    const size_t start = text.lineStarts[line];
    const size_t end = line + 1 < text.lineStarts.size() ? text.lineStarts[line + 1]
                                                         : text.bytes.size();
    return text.bytes.substr(start, end - start);
}

} // namespace

// The engine on its own, without the command around it.
TEST(DiffEngineTest, EmptyFiles) {
    const DiffText empty = TextOf("");
    bool stopped = false;
    const auto script = ComputeDiff(empty, empty, DiffAnalysisOptions{},
                                    []() { return false; }, stopped);
    EXPECT_TRUE(script.empty());
    EXPECT_FALSE(stopped);

    const DiffText one = TextOf("x\n");
    const auto insert = ComputeDiff(empty, one, DiffAnalysisOptions{},
                                    []() { return false; }, stopped);
    ASSERT_EQ(insert.size(), 1u);
    EXPECT_EQ(insert[0].line0, 0);
    EXPECT_EQ(insert[0].line1, 0);
    EXPECT_EQ(insert[0].deleted, 0);
    EXPECT_EQ(insert[0].inserted, 1);
}

TEST(DiffEngineTest, AllInserted) {
    std::string text;
    for (int i = 0; i < 100; ++i) {
        text += "line " + std::to_string(i) + "\n";
    }
    const DiffText empty = TextOf("");
    bool stopped = false;
    const auto script = ComputeDiff(empty, TextOf(text), DiffAnalysisOptions{},
                                    []() { return false; }, stopped);
    ASSERT_EQ(script.size(), 1u);
    EXPECT_EQ(script[0].deleted, 0);
    EXPECT_EQ(script[0].inserted, 100);
}

TEST(DiffEngineTest, MinimalAgainstLcs) {
    // The longest common subsequence of "a b" and "b a" is one line, so no
    // script can do better than one deletion and one insertion.
    const DiffText a = TextOf("a\nb\n");
    const DiffText b = TextOf("b\na\n");
    bool stopped = false;
    const auto script = ComputeDiff(a, b, DiffAnalysisOptions{},
                                    []() { return false; }, stopped);
    size_t operations = 0;
    for (const DiffChange& change : script) {
        operations += change.deleted + change.inserted;
    }
    EXPECT_EQ(operations, 2u);
}

TEST(DiffEngineTest, LargeInputsStillCorrect) {
    // A thousand lines with every tenth changed: the script must move the
    // unchanged lines across whole, and delete and insert exactly the
    // changed ones (100 deletions, 100 insertions).
    std::string aText, bText;
    for (int i = 0; i < 1000; ++i) {
        aText += "line " + std::to_string(i) + "\n";
        bText += "line " + std::to_string(i) + (i % 10 == 5 ? " changed" : "") + "\n";
    }
    const DiffText a = TextOf(aText);
    const DiffText b = TextOf(bText);
    bool stopped = false;
    const auto script = ComputeDiff(a, b, DiffAnalysisOptions{},
                                    []() { return false; }, stopped);
    ASSERT_FALSE(script.empty());

    // Every change replaces one line, and the matched lines between the
    // changes are equal in both files.
    size_t deleted = 0;
    size_t inserted = 0;
    size_t prevEnd0 = 0;
    size_t prevEnd1 = 0;
    for (const DiffChange& change : script) {
        // The unchanged run before this change matches.
        const std::string beforeA = LineTextOf(a, prevEnd0);
        const std::string beforeB = LineTextOf(b, prevEnd1);
        EXPECT_EQ(beforeA, beforeB);
        EXPECT_EQ(change.deleted, 1);
        EXPECT_EQ(change.inserted, 1);
        deleted += static_cast<size_t>(change.deleted);
        inserted += static_cast<size_t>(change.inserted);
        prevEnd0 = static_cast<size_t>(change.line0 + change.deleted);
        prevEnd1 = static_cast<size_t>(change.line1 + change.inserted);
    }
    EXPECT_EQ(deleted, 100u);
    EXPECT_EQ(inserted, 100u);
}

TEST(DiffEngineTest, StopEndsEarly) {
    const DiffText a = TextOf("a\nb\n");
    const DiffText b = TextOf("a\nc\n");
    bool stopped = false;
    const auto script = ComputeDiff(a, b, DiffAnalysisOptions{},
                                    []() { return true; }, stopped);
    EXPECT_TRUE(stopped);
    EXPECT_TRUE(script.empty());
}
} // namespace Haisos
