#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "BuiltinDate.h"
#include "commands/diff/DiffEngine.h"
#include "commands/diff/Diff.h"

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

// The directory tree the directory-comparison tests run against, under /t
// (run with workingDirectory "/t"). ra and rb hold the pairs: a differing
// file pair (x), a differing pair under a common subdirectory (s/y), a file
// only in ra (only), a directory only in ra (onlydir), a directory only in
// rb (t), a file against a directory (kind), a binary pair (bin). The
// extras: x a plain file, xf one that no ra entry matches, exl an -X
// pattern list, na/nb a pair for -N, ta/tb a file-against-directory pair
// met inside the walk. Each expectation below was observed from GNU
// diffutils 3.10 under LC_ALL=C TZ=UTC on this very tree.
void MakeDiffTree(const std::shared_ptr<IFileSystem>& fs) {
    auto write = [&](const std::string& path, const std::string& content) {
        auto file = fs->OpenFile(path, kFileOpenWriteCreateTruncate, kFileCreateMode);
        ASSERT_NE(file, nullptr) << path;
        file->Write(content.data(), content.size());
    };
    auto makeDir = [&](const std::string& path) {
        ASSERT_EQ(fs->CreateDirectory(path, kDirMode), 0) << path;
    };
    makeDir("/t");
    makeDir("/t/ra");
    makeDir("/t/rb");
    makeDir("/t/ra/s");
    makeDir("/t/rb/s");
    makeDir("/t/ra/onlydir");
    makeDir("/t/rb/t");
    makeDir("/t/rb/kind");
    write("/t/ra/x", "1\n");
    write("/t/rb/x", "2\n");
    write("/t/ra/s/y", "a\n");
    write("/t/rb/s/y", "b\n");
    write("/t/ra/only", "q\n");
    write("/t/ra/kind", "k\n");
    write("/t/ra/bin", std::string("x\0y\n", 5));
    write("/t/rb/bin", std::string("x\0z\n", 5));
    write("/t/x", "1\n");
    write("/t/xf", "1\n");
    write("/t/exl", "only\ns*\n");
    makeDir("/t/na");
    makeDir("/t/nb");
    write("/t/nb/new", "n\n");
    makeDir("/t/ta");
    makeDir("/t/tb");
    makeDir("/t/tb/e");
    write("/t/ta/e", "");
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

// The fixed ends (the horizon) keep a change from sliding into the lines
// both files share; a wider horizon lets it move lower.
TEST_F(BuiltinCommandsTest, DiffHorizonMovesChanges) {
    WriteFile("/h1", "b\na\n");
    WriteFile("/h2", "a\nb\na\na\n");
    auto captured = RunCaptured("diff", {"h1", "h2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "0a1\n> a\n1a3\n> a\n");
    EXPECT_EQ(captured.status, 1);

    // Unified output widens the horizon to the context length, so the
    // second insertion lands one line lower.
    captured = RunCaptured("diff", {"-u", "-L", "x", "-L", "y", "h1", "h2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "--- x\n"
              "+++ y\n"
              "@@ -1,2 +1,4 @@\n"
              "+a\n"
              " b\n"
              " a\n"
              "+a\n");

    captured = RunCaptured("diff", {"-U0", "-L", "x", "-L", "y", "h1", "h2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "--- x\n"
              "+++ y\n"
              "@@ -0,0 +1 @@\n"
              "+a\n"
              "@@ -1,0 +3 @@\n"
              "+a\n");

    // The same widening asked for outright.
    captured = RunCaptured("diff", {"--horizon-lines=3", "h1", "h2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "0a1\n> a\n2a4\n> a\n");
    EXPECT_EQ(captured.status, 1);
}

// Two scripts of four changed lines are possible here (keep the `b` or keep
// the `c`); the engine takes the one that deletes earlier.
TEST_F(BuiltinCommandsTest, DiffPicksMyersScript) {
    WriteFile("/p1", "a\nb\nc\n");
    WriteFile("/p2", "c\nb\nb\n");
    const auto captured = RunCaptured("diff", {"p1", "p2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1,2d0\n< a\n< b\n3a2,3\n> b\n> b\n");
    EXPECT_EQ(captured.status, 1);
}

// Blocks slide to the lowest position they can reach, and line up with the
// other file's changes where one is possible.
TEST_F(BuiltinCommandsTest, DiffPlacesBlocks) {
    WriteFile("/q1", "a\nb\n");
    WriteFile("/q2", "a\nb\na\nb\n");
    auto captured = RunCaptured("diff", {"q1", "q2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "2a3,4\n> a\n> b\n");
    EXPECT_EQ(captured.status, 1);

    WriteFile("/q3", "x\ny\nx\ny\n");
    WriteFile("/q4", "x\ny\n");
    captured = RunCaptured("diff", {"q3", "q4"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "3,4d2\n< x\n< y\n");
    EXPECT_EQ(captured.status, 1);

    // The deleted `a` could be line 1 or 2; at line 1 it forms one change
    // with the inserted `c`, so it sits there.
    WriteFile("/q5", "a\na\nb\n");
    WriteFile("/q6", "c\na\nb\nc\n");
    captured = RunCaptured("diff", {"q5", "q6"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1c1\n< a\n---\n> c\n3a4\n> c\n");
    EXPECT_EQ(captured.status, 1);

    WriteFile("/q7", "{\n  a;\n}\n{\n  b;\n}\n");
    WriteFile("/q8", "{\n  a;\n}\n{\n  x;\n}\n{\n  b;\n}\n");
    captured = RunCaptured("diff", {"q7", "q8"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "4a5,7\n>   x;\n> }\n> {\n");
    EXPECT_EQ(captured.status, 1);
}

// The documented exception: the hunks are always the minimal script, which
// is what GNU's diff -d prints. GNU's default (no -d) prints 1d0, 2a2,4,
// 4,6c6 on this pair, placing the same number of changed lines differently.
TEST_F(BuiltinCommandsTest, DiffIsMinimalWhereGnuDefaultDiffers) {
    WriteFile("/m1", ".\na\na\na\n.\n.\n");
    WriteFile("/m2", "a\nc\nc\nc\na\nd\n");
    const std::string expected =
        "1,2d0\n"
        "< .\n"
        "< a\n"
        "3a2,4\n"
        "> c\n"
        "> c\n"
        "> c\n"
        "5,6c6\n"
        "< .\n"
        "< .\n"
        "---\n"
        "> d\n";
    auto captured = RunCaptured("diff", {"m1", "m2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, expected);
    EXPECT_EQ(captured.status, 1);

    // -d asks for minimality, which is what diff always prints.
    captured = RunCaptured("diff", {"-d", "m1", "m2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, expected);
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
    auto captured = RunCaptured("diff", {"-U0", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(BodyFrom(captured.out, "@@"),
              "@@ -2 +2 @@\n"
              "-b\n"
              "+B\n"
              "@@ -3,0 +4 @@\n"
              "+d\n"
              "\\ No newline at end of file\n");
    EXPECT_EQ(captured.status, 1);

    // An empty file: the old range is the line before the start, with a
    // count of zero -- the shape patch relies on.
    WriteFile("/e0", "");
    captured = RunCaptured("diff", {"-u", "-L", "x", "-L", "y", "e0", "a"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "--- x\n"
              "+++ y\n"
              "@@ -0,0 +1,3 @@\n"
              "+a\n"
              "+b\n"
              "+c\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, DiffUnifiedHeaderTimes) {
    MakeDiffFiles(root);
    auto captured = RunCaptured("diff", {"-u", "s1", "s2"}, std::nullopt, "/");
    // The header times are the files' own, in the ISO form with nanoseconds
    // and a numeric zone, exactly as FormatDateTime renders them.
    FileStatus status;
    ASSERT_EQ(root->Stat("/s1", status), 0);
    const FileDateTime t1 = status.modificationTime;
    ASSERT_EQ(root->Stat("/s2", status), 0);
    const FileDateTime t2 = status.modificationTime;
    const std::string first =
        "--- s1\t" + FormatDateTime("%Y-%m-%d %H:%M:%S.%N %z", t1, false) + "\n";
    const std::string second =
        "+++ s2\t" + FormatDateTime("%Y-%m-%d %H:%M:%S.%N %z", t2, false) + "\n";
    ASSERT_GE(captured.out.size(), first.size() + second.size());
    EXPECT_EQ(captured.out.substr(0, first.size()), first) << captured.out;
    EXPECT_EQ(captured.out.substr(first.size(), second.size()), second) << captured.out;
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

    // Names that need quoting are quoted in headers, with the special bytes
    // as C escapes and the others as octal -- only the first line is checked
    // (it holds the name; the second is the same shape).
    auto firstLineIs = [&](const std::string& name, const std::string& line) {
        const auto quoted = RunCaptured("diff", {"-u", name, "a"}, std::nullopt, "/");
        EXPECT_EQ(quoted.out.substr(0, line.size()), line) << quoted.out;
    };
    WriteFile("/sp ace", "q\n");
    firstLineIs("sp ace", "--- \"sp ace\"\t");
    WriteFile(std::string("/x") + '\x01' + "y", "q\n");
    firstLineIs(std::string("x") + '\x01' + "y", "--- \"x\\001y\"\t");
    WriteFile("/\xC3\xA9", "q\n");
    firstLineIs("\xC3\xA9", "--- \"\\303\\251\"\t");

    // The -q report names the file plainly, quotes and all dropped.
    captured = RunCaptured("diff", {"-q", "sp ace", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "Files sp ace and b differ\n");
    EXPECT_EQ(captured.status, 1);
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

    // With labels, the whole output is fixed text.
    captured = RunCaptured("diff", {"-c", "-L", "a", "-L", "b", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "*** a\n"
              "--- b\n"
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

    // No context: two hunks, the first showing only the changed line on
    // each side, the second only the insertion.
    captured = RunCaptured("diff", {"-C", "0", "-L", "a", "-L", "b", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "*** a\n"
              "--- b\n"
              "***************\n"
              "*** 2 ****\n"
              "! b\n"
              "--- 2 ----\n"
              "! B\n"
              "***************\n"
              "*** 3 ****\n"
              "--- 4 ----\n"
              "+ d\n"
              "\\ No newline at end of file\n");
    EXPECT_EQ(captured.status, 1);

    // Deletion alone: the second range is the line before the start.
    WriteFile("/e0", "");
    captured = RunCaptured("diff", {"-c", "-L", "x", "-L", "y", "a", "e0"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "*** x\n"
              "--- y\n"
              "***************\n"
              "*** 1,3 ****\n"
              "- a\n"
              "- b\n"
              "- c\n"
              "--- 0 ----\n");
    EXPECT_EQ(captured.status, 1);

    // Without labels the headers carry the files' own times, in the C
    // locale's default form.
    captured = RunCaptured("diff", {"-c", "a", "b"}, std::nullopt, "/");
    FileStatus status;
    ASSERT_EQ(root->Stat("/a", status), 0);
    const std::string first =
        "*** a\t" + FormatDateTime("%a %b %e %T %Y", status.modificationTime, false) + "\n";
    ASSERT_EQ(root->Stat("/b", status), 0);
    const std::string second =
        "--- b\t" + FormatDateTime("%a %b %e %T %Y", status.modificationTime, false) + "\n";
    ASSERT_GE(captured.out.size(), first.size() + second.size());
    EXPECT_EQ(captured.out.substr(0, first.size()), first) << captured.out;
    EXPECT_EQ(captured.out.substr(first.size(), second.size()), second) << captured.out;
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

    // A line that is exactly "." is written "..", the insertion is closed,
    // the line is emptied with s/.//, and insertion resumes before the rest.
    WriteFile("/dot1", "y\n");
    WriteFile("/dot2", ".\nx\n");
    captured = RunCaptured("diff", {"-e", "dot1", "dot2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1c\n..\n.\ns/.//\na\nx\n.\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, DiffEdIdenticalMissingNewline) {
    MakeDiffFiles(root);
    // Two distinct files, identical, both missing the final newline: GNU
    // warns for each and exits 2, with no script and no identical line
    // even under -s. Only the same file stays silent.
    WriteFile("/q1", "a\nb");
    WriteFile("/q2", "a\nb");
    WriteFile("/qm", "a\nb\n");
    ASSERT_EQ(root->CreateDirectory("/qd", kDirMode), 0);

    auto captured = RunCaptured("diff", {"-e", "q1", "q2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
              "diff: q1: No newline at end of file\n"
              "\n"
              "diff: q2: No newline at end of file\n"
              "\n");
    EXPECT_EQ(captured.status, 2);

    // -s does not call them identical.
    captured = RunCaptured("diff", {"-s", "-e", "q1", "q2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
              "diff: q1: No newline at end of file\n"
              "\n"
              "diff: q2: No newline at end of file\n"
              "\n");
    EXPECT_EQ(captured.status, 2);

    // One side complete: only that side's warning.
    captured = RunCaptured("diff", {"-e", "q1", "qm"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "diff: q1: No newline at end of file\n\n");
    EXPECT_EQ(captured.status, 2);

    // The standard input against a file: the input warns as its side does.
    captured = RunCaptured("diff", {"-e", "-", "q2"}, "a\nb", "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
              "diff: -: No newline at end of file\n"
              "\n"
              "diff: q2: No newline at end of file\n"
              "\n");
    EXPECT_EQ(captured.status, 2);

    // The same file under every spelling: silent 0, and -s identical.
    for (const std::string& second : {"q1", "./q1", "qd/../q1"}) {
        captured = RunCaptured("diff", {"-e", "q1", second}, std::nullopt, "/");
        EXPECT_EQ(captured.out, "") << second;
        EXPECT_EQ(captured.err, "") << second;
        EXPECT_EQ(captured.status, 0) << second;
    }
    captured = RunCaptured("diff", {"-e", "-", "-"}, "a\nb", "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("diff", {"-s", "-e", "q1", "q1"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "Files q1 and q1 are identical\n");
    EXPECT_EQ(captured.status, 0);

    // -q compares the bytes as read: the identical pair is silent, the
    // pair differing only in the final newline differs.
    captured = RunCaptured("diff", {"-q", "-e", "q1", "q2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("diff", {"-q", "-e", "q1", "qm"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "Files q1 and qm differ\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, DiffFindLongOptionExactFirst) {
    // A table shaped so an exact match comes after two entries it prefixes:
    // the exact match wins; a prefix of both is ambiguous.
    const std::vector<BuiltinOption> table = {
        {'q', "", 4, BuiltinArgument::None},
        {' ', "ab-x", 1, BuiltinArgument::None},
        {' ', "ab-y", 2, BuiltinArgument::None},
        {' ', "ab", 3, BuiltinArgument::None},
    };
    EXPECT_EQ(DiffFindLongOption("ab", table)->id, 3);
    EXPECT_EQ(DiffFindLongOption("ab-x", table)->id, 1);
    EXPECT_EQ(DiffFindLongOption("ab-y", table)->id, 2);
    EXPECT_EQ(DiffFindLongOption("ab-", table), nullptr);
    EXPECT_EQ(DiffFindLongOption("a", table), nullptr);
    // A short-only option's empty long name matches no long option.
    EXPECT_EQ(DiffFindLongOption("", table), nullptr);
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

    // The three-line pair the whitespace options each judge differently.
    WriteFile("/w3", "a b\nfoo  bar\nend \n");
    WriteFile("/w4", "a  b\nfoo bar\nend\n");
    captured = RunCaptured("diff", {"w3", "w4"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "1,3c1,3\n"
              "< a b\n"
              "< foo  bar\n"
              "< end \n"
              "---\n"
              "> a  b\n"
              "> foo bar\n"
              "> end\n");
    EXPECT_EQ(captured.status, 1);

    // -b and -w see all three lines as equal.
    captured = RunCaptured("diff", {"-b", "w3", "w4"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("diff", {"-w", "w3", "w4"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    // -Z only drops the trailing run, so the first two lines still differ.
    captured = RunCaptured("diff", {"-Z", "w3", "w4"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1,2c1,2\n< a b\n< foo  bar\n---\n> a  b\n> foo bar\n");
    EXPECT_EQ(captured.status, 1);

    // -E expands tabs to the next column; a backspace ends the column
    // counting, so no spaces equal a tab after one.
    WriteFile("/w5", "a\tb\n");
    WriteFile("/w6", "a       b\n");
    captured = RunCaptured("diff", {"-E", "w5", "w6"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("diff", {"-E", "-Z", "w5", "w6"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1c1\n< a\tb\n---\n> a       b\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("diff", {"-Z", "-E", "w5", "w6"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1c1\n< a\tb\n---\n> a       b\n");
    EXPECT_EQ(captured.status, 1);

    WriteFile("/w7", "a\bc\tb\n");
    WriteFile("/w8", "a\bc       b\n");
    captured = RunCaptured("diff", {"-E", "w7", "w8"}, std::nullopt, "/");
    EXPECT_EQ(captured.status, 1);

    // An incomplete last line equals only the other file's incomplete one:
    // -b and -Z key it like any line, -i keeps the marker.
    WriteFile("/w9", "a\nb");
    WriteFile("/w10", "a\nb\n");
    captured = RunCaptured("diff", {"-b", "w9", "w10"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("diff", {"-Z", "w9", "w10"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("diff", {"-i", "w9", "w10"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "2c2\n"
              "< b\n"
              "\\ No newline at end of file\n"
              "---\n"
              "> b\n");
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

    // A blank line on one side against the other: -B drops the change whole.
    WriteFile("/g1", "a\n\nb\n");
    WriteFile("/g2", "a\nb\n\n");
    captured = RunCaptured("diff", {"g1", "g2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "2d1\n< \n3a3\n> \n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("diff", {"-B", "g1", "g2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("diff", {"-B", "-q", "g1", "g2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);

    // A real change next to an ignorable one: the ignorable is dropped from
    // normal output but kept, marked as usual, in the unified hunk.
    WriteFile("/g3", "a\n\nb\nc\n");
    WriteFile("/g4", "a\nb\nX\nc\n");
    captured = RunCaptured("diff", {"-B", "g3", "g4"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "3a3\n> X\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("diff", {"-B", "-u", "-L", "x", "-L", "y", "g3", "g4"},
                           std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "--- x\n"
              "+++ y\n"
              "@@ -1,4 +1,4 @@\n"
              " a\n"
              "-\n"
              " b\n"
              "+X\n"
              " c\n");
    EXPECT_EQ(captured.status, 1);

    // A line of spaces counts as blank only with -Z or stronger.
    WriteFile("/g5", "a\n  \nb\n");
    WriteFile("/g6", "a\nb\n");
    captured = RunCaptured("diff", {"-B", "g5", "g6"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "2d1\n<   \n");
    EXPECT_EQ(captured.status, 1);

    for (const char* option : {"-Z", "-b", "-w"}) {
        captured = RunCaptured("diff", {"-B", option, "g5", "g6"}, std::nullopt, "/");
        EXPECT_EQ(captured.out, "") << option;
        EXPECT_EQ(captured.status, 0) << option;
    }

    // An ignorable change joins a nearby hunk: with context 3, the blank
    // line 2 lines below the real change is pulled in; one line lower still.
    WriteFile("/j1", "R1\n1\n2\n\n3\n4\n5\n6\n7\n8\n9\n10\n11\n");
    WriteFile("/j2", "Q1\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n");
    captured = RunCaptured("diff", {"-B", "-u", "-L", "x", "-L", "y", "j1", "j2"},
                           std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "--- x\n"
              "+++ y\n"
              "@@ -1,7 +1,6 @@\n"
              "-R1\n"
              "+Q1\n"
              " 1\n"
              " 2\n"
              "-\n"
              " 3\n"
              " 4\n"
              " 5\n");
    EXPECT_EQ(captured.status, 1);

    // Moved 3 lines below the real change, it no longer joins the hunk.
    WriteFile("/j3", "R1\n1\n2\n3\n\n4\n5\n6\n7\n8\n9\n10\n11\n");
    captured = RunCaptured("diff", {"-B", "-u", "-L", "x", "-L", "y", "j3", "j2"},
                           std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "--- x\n"
              "+++ y\n"
              "@@ -1,4 +1,4 @@\n"
              "-R1\n"
              "+Q1\n"
              " 1\n"
              " 2\n"
              " 3\n");
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

    // -T on the usual pair: a tab between every mark and its text.
    captured = RunCaptured("diff", {"-T", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "2c2\n"
              "<\tb\n"
              "---\n"
              ">\tB\n"
              "3a4\n"
              ">\td\n"
              "\\ No newline at end of file\n");
    EXPECT_EQ(captured.status, 1);

    // -uT: the context line's tab replaces the leading space, too.
    captured = RunCaptured("diff", {"-uT", "-L", "a", "-L", "b", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "--- a\n"
              "+++ b\n"
              "@@ -1,3 +1,4 @@\n"
              "\ta\n"
              "-\tb\n"
              "+\tB\n"
              "\tc\n"
              "+\td\n"
              "\\ No newline at end of file\n");
    EXPECT_EQ(captured.status, 1);

    // -t with a carriage return: the column count restarts at it, and in
    // normal output the mark is written again after it, nine spaces deep.
    WriteFile("/r1", "a\r\tc\n");
    WriteFile("/r2", "x\n");
    captured = RunCaptured("diff", {"-t", "r1", "r2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "1c1\n"
              "< a\r<" + std::string(9, ' ') + "c\n"
              "---\n"
              "> x\n");
    EXPECT_EQ(captured.status, 1);

    // --suppress-blank-empty in both styles: the mark stands alone.
    WriteFile("/k1", "a\n\nb\n");
    WriteFile("/k2", "a\nb\n\n");
    captured = RunCaptured("diff", {"--suppress-blank-empty", "-u", "-L", "x", "-L", "y", "k1", "k2"},
                           std::nullopt, "/");
    EXPECT_EQ(captured.out,
              "--- x\n"
              "+++ y\n"
              "@@ -1,3 +1,3 @@\n"
              " a\n"
              "-\n"
              " b\n"
              "+\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("diff", {"--suppress-blank-empty", "k1", "k2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "2d1\n<\n3a3\n>\n");
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

    captured = RunCaptured("diff", {"a", "nope"}, std::nullopt, "/");
    EXPECT_EQ(captured.err, "diff: nope: No such file or directory\n");
    EXPECT_EQ(captured.status, 2);

    // No operands at all: the command itself is named.
    captured = RunCaptured("diff", {}, std::nullopt, "/");
    EXPECT_EQ(captured.err, "diff: missing operand after 'diff'\n" + tryHelp);
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("diff", {"-U", "x", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.err, "diff: invalid context length 'x'\n" + tryHelp);
    EXPECT_EQ(captured.status, 2);

    // Too many labels: reported without the Try line.
    captured = RunCaptured("diff", {"-L", "1", "-L", "2", "-L", "3", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.err, "diff: too many file label options\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("diff", {"--tabsize=0", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.err, "diff: invalid tabsize '0'\n" + tryHelp);
    EXPECT_EQ(captured.status, 2);

    // A not treated option is parsed and reported, and the diff goes on.
    captured = RunCaptured("diff", {"-y", "a", "b"}, std::nullopt, "/");
    EXPECT_EQ(captured.err, "Parameter -y is not treated by HaisosOS diff v. 1.1.1\n");
    EXPECT_EQ(captured.out,
              "2c2\n"
              "< b\n"
              "---\n"
              "> B\n"
              "3a4\n"
              "> d\n"
              "\\ No newline at end of file\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("diff", {"-v"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "diff (HaisosOS builtin) 1.1.1\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, DiffHelpAndVersion) {
    MakeDiffFiles(root);
    const auto help = RunCaptured("diff", {"--help"}, std::nullopt, "/");
    EXPECT_EQ(help.status, 0);
    EXPECT_NE(help.out.find("HaisosOS diff version 1.1.1"), std::string::npos);
    EXPECT_NE(help.out.find("Not treated arguments:"), std::string::npos);

    const auto version = RunCaptured("diff", {"--version"}, std::nullopt, "/");
    EXPECT_EQ(version.out, "diff (HaisosOS builtin) 1.1.1\n");
    EXPECT_EQ(version.status, 0);

    // A not treated option is parsed, reported and gone past.
    const auto captured = RunCaptured("diff", {"-l", "a", "c"}, std::nullopt, "/");
    EXPECT_EQ(captured.err, "Parameter -l is not treated by HaisosOS diff v. 1.1.1\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);
}

// One level of two directories, without -r: every kind of entry pair in
// byte order of the names, and the differing file pair's output.
TEST_F(BuiltinCommandsTest, DiffDirOneLevel) {
    MakeDiffTree(root);
    const auto captured = RunCaptured("diff", {"ra", "rb"}, std::nullopt, "/t");
    EXPECT_EQ(captured.out,
              "Binary files ra/bin and rb/bin differ\n"
              "File ra/kind is a regular file while file rb/kind is a directory\n"
              "Only in ra: only\n"
              "Only in ra: onlydir\n"
              "Common subdirectories: ra/s and rb/s\n"
              "Only in rb: t\n"
              "diff ra/x rb/x\n"
              "1c1\n"
              "< 1\n"
              "---\n"
              "> 2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);
}

// -r descends into the common subdirectory, and the pair header carries the
// options as typed. No header goes before a binary or type-mismatch report.
TEST_F(BuiltinCommandsTest, DiffDirRecursive) {
    MakeDiffTree(root);
    const auto captured = RunCaptured("diff", {"-r", "ra", "rb"}, std::nullopt, "/t");
    EXPECT_EQ(captured.out,
              "Binary files ra/bin and rb/bin differ\n"
              "File ra/kind is a regular file while file rb/kind is a directory\n"
              "Only in ra: only\n"
              "Only in ra: onlydir\n"
              "diff -r ra/s/y rb/s/y\n"
              "1c1\n"
              "< a\n"
              "---\n"
              "> b\n"
              "Only in rb: t\n"
              "diff -r ra/x rb/x\n"
              "1c1\n"
              "< 1\n"
              "---\n"
              "> 2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);
}

// -q briefs the file pairs (the binary one without its "Binary"), -x drops
// the names a pattern covers, and -X takes the same patterns from a file,
// one per line.
TEST_F(BuiltinCommandsTest, DiffDirBriefAndExclude) {
    MakeDiffTree(root);
    const auto brief = RunCaptured("diff", {"-rq", "-x", "s", "-x", "only", "ra", "rb"},
                                  std::nullopt, "/t");
    EXPECT_EQ(brief.out,
              "Files ra/bin and rb/bin differ\n"
              "File ra/kind is a regular file while file rb/kind is a directory\n"
              "Only in ra: onlydir\n"
              "Only in rb: t\n"
              "Files ra/x and rb/x differ\n");
    EXPECT_EQ(brief.status, 1);

    const auto excluded = RunCaptured("diff", {"-X", "exl", "-r", "ra", "rb"}, std::nullopt, "/t");
    EXPECT_EQ(excluded.out,
              "Binary files ra/bin and rb/bin differ\n"
              "File ra/kind is a regular file while file rb/kind is a directory\n"
              "Only in ra: onlydir\n"
              "Only in rb: t\n"
              "diff -X exl -r ra/x rb/x\n"
              "1c1\n"
              "< 1\n"
              "---\n"
              "> 2\n");
    EXPECT_EQ(excluded.err, "");
    EXPECT_EQ(excluded.status, 1);
}

// -N fakes a file that is only on one side, as empty bytes, with the pair
// header and the epoch time in its place.
TEST_F(BuiltinCommandsTest, DiffDirNewFile) {
    MakeDiffTree(root);
    const auto captured = RunCaptured("diff", {"-rN", "na", "nb"}, std::nullopt, "/t");
    EXPECT_EQ(captured.out,
              "diff -rN na/new nb/new\n"
              "0a1\n"
              "> n\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);
}

// -N at the top level too: a directory operand that is not there is faked
// as an empty directory of the same shape, so the walk covers the other
// side's entries -- one-sided directories becoming "Common subdirectories".
TEST_F(BuiltinCommandsTest, DiffDirNewFileTopLevel) {
    MakeDiffTree(root);
    const auto file = RunCaptured("diff", {"-r", "-N", "nb", "nofile"}, std::nullopt, "/t");
    EXPECT_EQ(file.out,
              "diff -r -N nb/new nofile/new\n"
              "1d0\n"
              "< n\n");
    EXPECT_EQ(file.err, "");
    EXPECT_EQ(file.status, 1);

    const auto walk = RunCaptured("diff", {"-N", "ra", "nofile"}, std::nullopt, "/t");
    EXPECT_EQ(walk.out,
              "Binary files ra/bin and nofile/bin differ\n"
              "diff -N ra/kind nofile/kind\n"
              "1d0\n"
              "< k\n"
              "diff -N ra/only nofile/only\n"
              "1d0\n"
              "< q\n"
              "Common subdirectories: ra/onlydir and nofile/onlydir\n"
              "Common subdirectories: ra/s and nofile/s\n"
              "diff -N ra/x nofile/x\n"
              "1d0\n"
              "< 1\n");
    EXPECT_EQ(walk.err, "");
    EXPECT_EQ(walk.status, 1);
}

// A file and a directory operand: the file is compared with the same-named
// entry under the directory, which must be there even with -N.
TEST_F(BuiltinCommandsTest, DiffDirAndFile) {
    MakeDiffTree(root);
    const auto same = RunCaptured("diff", {"x", "ra"}, std::nullopt, "/t");
    EXPECT_EQ(same.out, "");
    EXPECT_EQ(same.err, "");
    EXPECT_EQ(same.status, 0);

    const auto flipped = RunCaptured("diff", {"ra", "x"}, std::nullopt, "/t");
    EXPECT_EQ(flipped.out, "");
    EXPECT_EQ(flipped.status, 0);

    const auto differ = RunCaptured("diff", {"x", "rb"}, std::nullopt, "/t");
    EXPECT_EQ(differ.out,
              "1c1\n"
              "< 1\n"
              "---\n"
              "> 2\n");
    EXPECT_EQ(differ.status, 1);

    // The joined entry is missing: the error names it, whichever side the
    // directory is on.
    const auto missing = RunCaptured("diff", {"xf", "ra"}, std::nullopt, "/t");
    EXPECT_EQ(missing.out, "");
    EXPECT_EQ(missing.err, "diff: ra/xf: No such file or directory\n");
    EXPECT_EQ(missing.status, 2);

    const auto missingFlipped = RunCaptured("diff", {"ra", "xf"}, std::nullopt, "/t");
    EXPECT_EQ(missingFlipped.out, "");
    EXPECT_EQ(missingFlipped.err, "diff: ra/xf: No such file or directory\n");
    EXPECT_EQ(missingFlipped.status, 2);
}

// -S starts the top level at FILE: in either directory, the names that sort
// before it are skipped. The header still carries the option.
TEST_F(BuiltinCommandsTest, DiffDirStartingFile) {
    MakeDiffTree(root);
    const auto captured = RunCaptured("diff", {"-r", "-S", "only", "ra", "rb"}, std::nullopt, "/t");
    EXPECT_EQ(captured.out,
              "Only in ra: only\n"
              "Only in ra: onlydir\n"
              "diff -r -S only ra/s/y rb/s/y\n"
              "1c1\n"
              "< a\n"
              "---\n"
              "> b\n"
              "Only in rb: t\n"
              "diff -r -S only ra/x rb/x\n"
              "1c1\n"
              "< 1\n"
              "---\n"
              "> 2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);
}

// Two directories whose same-named entries are a file and a directory: the
// type mismatch is reported inside the walk.
TEST_F(BuiltinCommandsTest, DiffDirTypes) {
    MakeDiffTree(root);
    const auto captured = RunCaptured("diff", {"ta", "tb"}, std::nullopt, "/t");
    EXPECT_EQ(captured.out,
              "File ta/e is a regular empty file while file tb/e is a directory\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);

    // Two devices inside the walk are reported by type, never read (GNU
    // 3.10 prints the line even when both types agree).
    auto devices = factory->CreateServicesCreator()->CreateFileSystemService();
    root->Mount("/d1", devices->CreateDeviceFileSystem());
    root->Mount("/d2", devices->CreateDeviceFileSystem());
    const auto walked = RunCaptured("diff", {"-r", "/d1", "/d2"}, std::nullopt, "/");
    EXPECT_EQ(walked.out,
              "File /d1/null is a character special file while file /d2/null is a character special file\n"
              "File /d1/zero is a character special file while file /d2/zero is a character special file\n");
    EXPECT_EQ(walked.err, "");
    EXPECT_EQ(walked.status, 1);
}

// The pair header carries every option word as typed, shell-quoted when a
// word needs it; the file headers under -u show the files' own times.
TEST_F(BuiltinCommandsTest, DiffDirOptionEcho) {
    MakeDiffTree(root);
    const auto quoted = RunCaptured("diff", {"-x", "o*", "-r", "ra", "rb"}, std::nullopt, "/t");
    EXPECT_EQ(quoted.out,
              "Binary files ra/bin and rb/bin differ\n"
              "File ra/kind is a regular file while file rb/kind is a directory\n"
              "diff -x 'o*' -r ra/s/y rb/s/y\n"
              "1c1\n"
              "< a\n"
              "---\n"
              "> b\n"
              "Only in rb: t\n"
              "diff -x 'o*' -r ra/x rb/x\n"
              "1c1\n"
              "< 1\n"
              "---\n"
              "> 2\n");
    EXPECT_EQ(quoted.err, "");
    EXPECT_EQ(quoted.status, 1);

    // The header times under -u are the files' own, each as FormatDateTime
    // renders them.
    const auto unified = RunCaptured("diff", {"-u", "-r", "ra", "rb"}, std::nullopt, "/t");
    const auto headerTime = [&](const std::string& path) {
        FileStatus status;
        EXPECT_EQ(root->Stat(path, status), 0);
        return FormatDateTime("%Y-%m-%d %H:%M:%S.%N %z", status.modificationTime, false);
    };
    EXPECT_EQ(unified.out,
              "Binary files ra/bin and rb/bin differ\n"
              "File ra/kind is a regular file while file rb/kind is a directory\n"
              "Only in ra: only\n"
              "Only in ra: onlydir\n"
              "diff -u -r ra/s/y rb/s/y\n"
              "--- ra/s/y\t" + headerTime("/t/ra/s/y") + "\n"
              "+++ rb/s/y\t" + headerTime("/t/rb/s/y") + "\n"
              "@@ -1 +1 @@\n"
              "-a\n"
              "+b\n"
              "Only in rb: t\n"
              "diff -u -r ra/x rb/x\n"
              "--- ra/x\t" + headerTime("/t/ra/x") + "\n"
              "+++ rb/x\t" + headerTime("/t/rb/x") + "\n"
              "@@ -1 +1 @@\n"
              "-1\n"
              "+2\n");
    EXPECT_EQ(unified.err, "");
    EXPECT_EQ(unified.status, 1);
}

// --from-file/--to-file compare one file with every operand, a directory
// operand through its same-named entry; with no operand there is nothing to
// compare and the status is 0.
TEST_F(BuiltinCommandsTest, DiffFromToFile) {
    MakeDiffTree(root);
    const auto from = RunCaptured("diff", {"--from-file=x", "ra", "rb"}, std::nullopt, "/t");
    EXPECT_EQ(from.out,
              "1c1\n"
              "< 1\n"
              "---\n"
              "> 2\n");
    EXPECT_EQ(from.err, "");
    EXPECT_EQ(from.status, 1);

    const auto to = RunCaptured("diff", {"--to-file=x", "rb", "x"}, std::nullopt, "/t");
    EXPECT_EQ(to.out,
              "1c1\n"
              "< 2\n"
              "---\n"
              "> 1\n");
    EXPECT_EQ(to.err, "");
    EXPECT_EQ(to.status, 1);

    const auto none = RunCaptured("diff", {"--from-file=x"}, std::nullopt, "/t");
    EXPECT_EQ(none.out, "");
    EXPECT_EQ(none.err, "");
    EXPECT_EQ(none.status, 0);

    // The joined entry is missing: the error names it.
    const auto missing = RunCaptured("diff", {"--from-file=xf", "ra"}, std::nullopt, "/t");
    EXPECT_EQ(missing.out, "");
    EXPECT_EQ(missing.err, "diff: ra/xf: No such file or directory\n");
    EXPECT_EQ(missing.status, 2);
}

// The remaining cases of the plan, each printed by GNU diff 3.10 (LC_ALL=C):
// -P and its one-way rule, '-' against a directory, the option conflicts,
// -ruN's epoch header, and a long option word that needs quoting.
TEST_F(BuiltinCommandsTest, DiffDirOperandRules) {
    MakeDiffTree(root);
    auto captured = RunCaptured("diff", {"-P", "nofile", "x"}, std::nullopt, "/t");
    EXPECT_EQ(captured.out, "0a1\n> 1\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("diff", {"--unidirectional-new-file", "x", "nofile"}, std::nullopt, "/t");
    EXPECT_EQ(captured.err, "diff: nofile: No such file or directory\n");
    EXPECT_EQ(captured.status, 2);
    captured = RunCaptured("diff", {"-", "ra"}, std::nullopt, "/t");
    EXPECT_EQ(captured.err, "diff: cannot compare '-' to a directory\n");
    EXPECT_EQ(captured.status, 2);
    captured = RunCaptured("diff", {"--from-file=a", "--to-file=b", "x"}, std::nullopt, "/t");
    EXPECT_EQ(captured.err, "diff: --from-file and --to-file both specified\n");
    EXPECT_EQ(captured.status, 2);
    captured = RunCaptured("diff", {"-S", "a", "-S", "b", "ra", "rb"}, std::nullopt, "/t");
    EXPECT_EQ(captured.err, "diff: conflicting -S option value 'b'\n"
                            "diff: Try 'diff --help' for more information.\n");
    EXPECT_EQ(captured.status, 2);
    captured = RunCaptured("diff", {"-X", "nope", "ra", "rb"}, std::nullopt, "/t");
    EXPECT_EQ(captured.err, "diff: nope: No such file or directory\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("diff", {"-ruN", "na", "nb"}, std::nullopt, "/t");
    const std::string epoch = FormatDateTime("%Y-%m-%d %H:%M:%S.%N %z", FileDateTime{0, 0}, false);
    EXPECT_EQ(captured.out.rfind("diff -ruN na/new nb/new\n--- na/new\t" + epoch + "\n+++ nb/new\t", 0), 0u)
        << captured.out;
    EXPECT_NE(captured.out.find("\n@@ -0,0 +1 @@\n+n\n"), std::string::npos) << captured.out;
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("diff", {"--recursive", "--exclude=a b", "ra", "rb"}, std::nullopt, "/t");
    EXPECT_NE(captured.out.find("diff --recursive '--exclude=a b' ra/x rb/x\n"), std::string::npos)
        << captured.out;
}

// Review regressions, each expectation printed by GNU diff 3.10 (LC_ALL=C).
TEST_F(BuiltinCommandsTest, DiffReviewRegressions) {
    MakeDiffFiles(root);
    // A block lines up with an insertion at the very end of the region.
    WriteFile("/e1", "a\na\n");
    WriteFile("/e2", "c\na\nc\n");
    auto captured = RunCaptured("diff", {"e1", "e2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "0a1\n> c\n2c3\n< a\n---\n> c\n");
    WriteFile("/e3", "x\na\na\ny\n");
    WriteFile("/e4", "x\nc\na\nc\ny\n");
    captured = RunCaptured("diff", {"e3", "e4"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1a2\n> c\n3c4\n< a\n---\n> c\n");
    // Boxes far taller or wider than they are deep.
    WriteFile("/o1", "a\nb\nc\nd\ne\n");
    WriteFile("/o2", "x\n");
    captured = RunCaptured("diff", {"o1", "o2"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1,5c1\n< a\n< b\n< c\n< d\n< e\n---\n> x\n");
    captured = RunCaptured("diff", {"o2", "o1"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "1c1,5\n< x\n---\n> a\n> b\n> c\n> d\n> e\n");
    // A common line is printed from file 0: only its missing newline is marked.
    WriteFile("/c0", "x\na\nb");
    WriteFile("/c1", "y\na\nb\n");
    captured = RunCaptured("diff", {"-u", "-b", "-L", "a", "-L", "b", "c0", "c1"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "--- a\n+++ b\n@@ -1,3 +1,3 @@\n-x\n+y\n a\n b\n\\ No newline at end of file\n");
    captured = RunCaptured("diff", {"-u", "-b", "-L", "a", "-L", "b", "c1", "c0"}, std::nullopt, "/");
    EXPECT_EQ(captured.out, "--- a\n+++ b\n@@ -1,3 +1,3 @@\n-y\n+x\n a\n b\n");
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

// |count| lines out of |distinct| equally likely ones, from a generator with
// a fixed seed, so every run sees the same texts.
std::string MakeLines(std::mt19937& rng, size_t count, unsigned distinct) {
    std::string text;
    for (size_t i = 0; i < count; ++i) {
        text += "L" + std::to_string(rng() % distinct) + "\n";
    }
    return text;
}

// Rebuilds file 1 from file 0 and the change list, checking as it walks that
// each change's line numbers are exactly where the walk is.
std::string Apply(const DiffText& a, const DiffText& b, const std::vector<DiffChange>& changes) {
    std::string result;
    size_t i0 = 0;
    size_t i1 = 0;
    for (const DiffChange& change : changes) {
        while (i0 < static_cast<size_t>(change.line0)) {
            result += LineTextOf(a, i0);
            ++i0;
            ++i1;
        }
        EXPECT_EQ(change.line0, static_cast<int64_t>(i0));
        EXPECT_EQ(change.line1, static_cast<int64_t>(i1));
        EXPECT_GE(change.deleted, 0);
        EXPECT_GE(change.inserted, 0);
        EXPECT_LE(static_cast<size_t>(change.line0 + change.deleted), a.LineCount());
        EXPECT_LE(static_cast<size_t>(change.line1 + change.inserted), b.LineCount());
        i0 += static_cast<size_t>(change.deleted);
        for (int64_t j = 0; j < change.inserted && i1 < b.LineCount(); ++j) {
            result += LineTextOf(b, i1);
            ++i1;
        }
    }
    while (i0 < a.LineCount()) {
        result += LineTextOf(a, i0);
        ++i0;
        ++i1;
    }
    return result;
}

// The longest common subsequence of the two files' lines, by the classic
// quadratic table over the lines' bytes.
size_t Lcs(const DiffText& a, const DiffText& b) {
    const size_t n = a.LineCount();
    const size_t m = b.LineCount();
    std::vector<std::vector<uint32_t>> table(n + 1, std::vector<uint32_t>(m + 1, 0));
    for (size_t i = n; i-- > 0;) {
        for (size_t j = m; j-- > 0;) {
            table[i][j] = LineTextOf(a, i) == LineTextOf(b, j)
                              ? table[i + 1][j + 1] + 1
                              : std::max(table[i + 1][j], table[i][j + 1]);
        }
    }
    return table[0][0];
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
    // 300 random pairs over four distinct lines: the script's changed lines
    // are exactly what the longest common subsequence leaves behind, and
    // applying it rebuilds file 1 -- with and without a horizon.
    std::mt19937 rng(20240102);
    for (int pair = 0; pair < 300; ++pair) {
        const std::string aText = MakeLines(rng, rng() % 41, 4);
        const std::string bText = MakeLines(rng, rng() % 41, 4);
        const DiffText a = TextOf(aText);
        const DiffText b = TextOf(bText);
        const size_t lcs = Lcs(a, b);
        const int64_t minimal = static_cast<int64_t>(a.LineCount() + b.LineCount() - 2 * lcs);
        DiffAnalysisOptions options;
        for (int pass = 0; pass < 2; ++pass, options.horizonLines = 3) {
            bool stopped = false;
            const auto script = ComputeDiff(a, b, options, []() { return false; }, stopped);
            ASSERT_FALSE(stopped) << pair;
            int64_t operations = 0;
            for (const DiffChange& change : script) {
                operations += change.deleted + change.inserted;
            }
            EXPECT_EQ(operations, minimal) << pair << ", pass " << pass;
            EXPECT_EQ(Apply(a, b, script), bText) << pair << ", pass " << pass;
        }
    }
}

TEST(DiffEngineTest, LargeInputsStillCorrect) {
    // Two 5,000-line files over 40 distinct lines: applying the script
    // rebuilds file 1, and no change names a line outside its file.
    std::mt19937 rng(20240103);
    const std::string aText = MakeLines(rng, 5000, 40);
    const std::string bText = MakeLines(rng, 5000, 40);
    const DiffText a = TextOf(aText);
    const DiffText b = TextOf(bText);
    bool stopped = false;
    const auto script = ComputeDiff(a, b, DiffAnalysisOptions{},
                                    []() { return false; }, stopped);
    ASSERT_FALSE(stopped);
    for (const DiffChange& change : script) {
        EXPECT_GE(change.line0, 0);
        EXPECT_LE(static_cast<size_t>(change.line0 + change.deleted), a.LineCount());
        EXPECT_GE(change.line1, 0);
        EXPECT_LE(static_cast<size_t>(change.line1 + change.inserted), b.LineCount());
    }
    EXPECT_EQ(Apply(a, b, script), bText);
}

TEST(DiffEngineTest, BlockPlacementFinishesQuickly) {
    // A long run of equal lines, its lower half interleaved with insertions
    // in the second file: the run's changed lines slide and merge down
    // through the placement, which keeps its unchanged-line count as a
    // running value instead of recounting the region per merge.
    const char* kExpected[] = {
        "[1,1,0,1]", "[2,3,0,1]", "[3,5,0,1]", "[4,7,0,1]",
        "[5,9,0,1]", "[6,11,0,1]", "[7,13,0,1]", "[8,15,8,1]",
    };
    std::string aText, bText;
    for (int i = 0; i < 16; ++i) {
        aText += "x\n";
    }
    for (int i = 0; i < 8; ++i) {
        bText += "x\nu" + std::to_string(i) + "\n";
    }
    bool stopped = false;
    const auto script = ComputeDiff(TextOf(aText), TextOf(bText), DiffAnalysisOptions{},
                                     []() { return false; }, stopped);
    ASSERT_EQ(script.size(), 8u);
    for (size_t i = 0; i < script.size(); ++i) {
        const std::string got = "[" + std::to_string(script[i].line0) + "," + std::to_string(script[i].line1)
            + "," + std::to_string(script[i].deleted) + "," + std::to_string(script[i].inserted) + "]";
        EXPECT_STREQ(got.c_str(), kExpected[i]);
    }

    // The same shape at 20,000 lines a side finishes well within the bound.
    const int equal = 10000;
    aText.clear();
    bText.clear();
    for (int i = 0; i < 2 * equal; ++i) {
        aText += "x\n";
    }
    for (int i = 0; i < equal; ++i) {
        bText += "x\nu" + std::to_string(i) + "\n";
    }
    const auto start = std::chrono::steady_clock::now();
    const auto big = ComputeDiff(TextOf(aText), TextOf(bText), DiffAnalysisOptions{},
                                 []() { return false; }, stopped);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    ASSERT_LT(elapsed.count(), 10000);
    ASSERT_EQ(big.size(), static_cast<size_t>(equal));
    int64_t deleted = 0, inserted = 0;
    for (const DiffChange& change : big) {
        deleted += change.deleted;
        inserted += change.inserted;
    }
    EXPECT_EQ(deleted, equal);
    EXPECT_EQ(inserted, equal);
    EXPECT_EQ(big.back().deleted, equal);
    EXPECT_EQ(Apply(TextOf(aText), TextOf(bText), big), bText);
}

TEST(DiffEngineTest, LargeVeryDifferentPair) {
    // A large pair with no line in common: the search runs to its full
    // depth, one change covering each file, and the script still rebuilds
    // file 1. The search tables are not copied per step.
    std::mt19937 rng(20240104);
    const int lines = 3000;
    std::string aText, bText;
    for (int i = 0; i < lines; ++i) {
        aText += "a" + std::to_string(rng() % 60) + "\n";
        bText += "b" + std::to_string(rng() % 60) + "\n";
    }
    const DiffText a = TextOf(aText);
    const DiffText b = TextOf(bText);
    bool stopped = false;
    const auto script = ComputeDiff(a, b, DiffAnalysisOptions{}, []() { return false; }, stopped);
    ASSERT_FALSE(stopped);
    ASSERT_EQ(script.size(), 1u);
    EXPECT_EQ(script[0].deleted, lines);
    EXPECT_EQ(script[0].inserted, lines);
    EXPECT_EQ(Apply(a, b, script), bText);
}

TEST(DiffEngineTest, StopEndsEarly) {
    const DiffText a = TextOf("a\nb\n");
    const DiffText b = TextOf("a\nc\n");
    bool stopped = false;
    const auto script = ComputeDiff(a, b, DiffAnalysisOptions{},
                                    []() { return true; }, stopped);
    EXPECT_TRUE(stopped);
    EXPECT_TRUE(script.empty());

    // The same on the large pair: stopped at once, nothing comes out.
    std::mt19937 rng(20240104);
    const DiffText bigA = TextOf(MakeLines(rng, 5000, 40));
    const DiffText bigB = TextOf(MakeLines(rng, 5000, 40));
    stopped = false;
    const auto empty = ComputeDiff(bigA, bigB, DiffAnalysisOptions{},
                                   []() { return true; }, stopped);
    EXPECT_TRUE(stopped);
    EXPECT_TRUE(empty.empty());
}
} // namespace Haisos
