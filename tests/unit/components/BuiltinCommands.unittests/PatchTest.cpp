#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"

namespace Haisos {
namespace {

// The patch every expectation below was observed from GNU patch 2.7.6
// against (LC_ALL=C, no terminal); P1 is the plan's recurring example.
constexpr char kP1[] =
    "--- f\n"
    "+++ g\n"
    "@@ -2,7 +2,7 @@\n"
    " 2\n"
    " 3\n"
    " 4\n"
    "-5\n"
    "+five\n"
    " 6\n"
    " 7\n"
    " 8\n";

// The lines `1\n` .. `10\n`.
std::string Seq(size_t count) {
    std::string text;
    for (size_t i = 1; i <= count; ++i) {
        text += std::to_string(i) + "\n";
    }
    return text;
}

// The directory the patch tests work in, with a file written into it.
void MakePatchDir(const std::shared_ptr<IFileSystem>& fs) {
    ASSERT_EQ(fs->CreateDirectory("/p", kDirMode), 0);
}

void WritePatchFile(const std::shared_ptr<IFileSystem>& fs, const std::string& name,
                    const std::string& content) {
    auto file = fs->OpenFile("/p/" + name, kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(file, nullptr) << name;
    file->Write(content.data(), content.size());
}

std::string ReadPatchFile(const std::shared_ptr<IFileSystem>& fs, const std::string& name) {
    std::string content;
    ReadWholeFile(*fs, "/p/" + name, content);
    return content;
}

bool PatchFileExists(const std::shared_ptr<IFileSystem>& fs, const std::string& name) {
    FileStatus status;
    return fs->Stat("/p/" + name, status) == 0;
}

} // namespace

// A unified diff applies at its stated place; no .orig, no temporary file.
TEST_F(BuiltinCommandsTest, PatchAppliesUnified) {
    MakePatchDir(root);
    WriteFile("/p/f", Seq(10));
    const Captured result = RunCaptured("patch", {"-p0"}, kP1, "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), "1\n2\n3\n4\nfive\n6\n7\n8\n9\n10\n");
    EXPECT_FALSE(PatchFileExists(root, "f.orig"));
    for (const DirectoryEntry& entry : root->ReadDirectory("/p")) {
        if (entry.name == "." || entry.name == "..") {
            continue;  // the directory's own entries
        }
        EXPECT_EQ(entry.name, "f") << entry.name;
    }
}

// A hunk found away from its stated place says so, and the original is kept.
TEST_F(BuiltinCommandsTest, PatchOffsetKeepsOrig) {
    MakePatchDir(root);
    WriteFile("/p/f", "x\ny\n" + Seq(10));
    const Captured result = RunCaptured("patch", {}, kP1, "/p");
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 4 (offset 2 lines).\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f.orig"), "x\ny\n" + Seq(10));
    EXPECT_EQ(ReadPatchFile(root, "f"), "x\ny\n1\n2\n3\n4\nfive\n6\n7\n8\n9\n10\n");

    // Two hunks, both at the same offset; both say it.
    WriteFile("/p/x", "z\nz\n" + std::string("a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\n"));
    const Captured two = RunCaptured("patch", {},
        "--- x\n"
        "+++ x\n"
        "@@ -1,3 +1,3 @@\n"
        " a\n"
        "-b\n"
        "+B\n"
        " c\n"
        "@@ -10,3 +10,3 @@\n"
        " j\n"
        "-k\n"
        "+K\n"
        " l\n", "/p");
    EXPECT_EQ(two.out,
        "patching file x\n"
        "Hunk #1 succeeded at 3 (offset 2 lines).\n"
        "Hunk #2 succeeded at 12 (offset 2 lines).\n");
    EXPECT_EQ(two.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "x"), "z\nz\na\nB\nc\nd\ne\nf\ng\nh\ni\nj\nK\nl\nm\n");
}

// The candidate order of the hunk search, the two anchors, overlapping
// hunks and an insertion with no old lines.
TEST_F(BuiltinCommandsTest, PatchSearchOrder) {
    MakePatchDir(root);
    const auto patchX = [&](const std::string& body) {
        return RunCaptured("patch", {}, "--- x\n+++ x\n" + body, "/p");
    };

    // At the same distance from the expected place, the later one is tried
    // first; then the earlier one.
    WriteFile("/p/x", "1\n2\np\nq\nr\n6\np\nq\nr\n10\n");
    Captured result = patchX("@@ -5,3 +5,3 @@\n p\n-q\n+Q\n r\n");
    EXPECT_EQ(result.out, "patching file x\nHunk #1 succeeded at 7 (offset 2 lines).\n");
    EXPECT_EQ(result.status, 0);

    WriteFile("/p/x", "1\n2\n3\np\nq\nr\n7\n8\np\nq\nr\n12\n");
    result = patchX("@@ -5,3 +5,3 @@\n p\n-q\n+Q\n r\n");
    EXPECT_EQ(result.out, "patching file x\nHunk #1 succeeded at 4 (offset -1 lines).\n");
    EXPECT_EQ(result.status, 0);

    // More leading than trailing context anchors the hunk at the file's end.
    WriteFile("/p/x", Seq(9) + "a\n");
    result = RunCaptured("patch", {"-F0"}, "--- x\n+++ x\n@@ -7,3 +7,3 @@\n 7\n 8\n-9\n+NINE\n", "/p");
    EXPECT_EQ(result.out,
        "patching file x\n"
        "Hunk #1 FAILED at 7.\n"
        "1 out of 1 hunk FAILED -- saving rejects to file x.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "x.rej"),
        "--- x\n+++ x\n@@ -7,3 +7,3 @@\n 7\n 8\n-9\n+NINE\n");

    // With the default fuzz the leading context is dropped instead: the
    // hunk matches at the only line it needs.
    result = patchX("@@ -7,3 +7,3 @@\n 7\n 8\n-9\n+NINE\n");
    EXPECT_EQ(result.out, "patching file x\nHunk #1 succeeded at 7 with fuzz 2.\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "x"), Seq(8) + "NINE\na\n");

    WriteFile("/p/x", "a\nb\n" + Seq(9));
    result = patchX("@@ -7,3 +7,3 @@\n 7\n 8\n-9\n+NINE\n");
    EXPECT_EQ(result.out, "patching file x\nHunk #1 succeeded at 9 (offset 2 lines).\n");
    EXPECT_EQ(result.status, 0);

    // Two hunks may share their context: the later starts on the earlier's
    // trailing lines.
    WriteFile("/p/x", "a\nb\nc\nd\ne\nf\n");
    result = patchX("@@ -1,4 +1,4 @@\n a\n-b\n+B\n c\n d\n"
                    "@@ -3,4 +3,4 @@\n c\n d\n-e\n+E\n f\n");
    EXPECT_EQ(result.out, "patching file x\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "x"), "a\nB\nc\nd\nE\nf\n");

    // An insertion with no old lines goes where the patch says it does.
    WriteFile("/p/x", "1\n");
    result = patchX("@@ -3,0 +4 @@\n+X\n");
    EXPECT_EQ(result.out, "patching file x\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "x"), "1\nX\n");

    // A hunk whose old lines all lie in an earlier hunk's ground fails.
    WriteFile("/p/x", "s\nt\nu\n4\n5\n6\n7\n8\n9\n10\n");
    result = patchX("@@ -1,10 +1,10 @@\n s\n t\n u\n 4\n 5\n 6\n 7\n 8\n 9\n-10\n+ten\n"
                    "@@ -10,3 +10,3 @@\n 8\n 9\n-10\n+X\n");
    EXPECT_EQ(result.out,
        "patching file x\n"
        "Hunk #2 FAILED at 10.\n"
        "1 out of 2 hunks FAILED -- saving rejects to file x.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "x"), "s\nt\nu\n4\n5\n6\n7\n8\n9\nten\n");

    // A stated line far beyond the file is searched from the file's end at
    // once (GNU patch 2.7.6 -F0: the same output).
    WriteFile("/p/x", "z\np\nq\nr\n");
    result = patchX("@@ -1000000000000,3 +1000000000000,3 @@\n p\n-q\n+Q\n r\n");
    EXPECT_EQ(result.out,
        "patching file x\n"
        "Hunk #1 succeeded at 2 (offset -999999999998 lines).\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "x"), "z\np\nQ\nr\n");
}

// A patch already applied, detected on its first hunk, with no terminal to
// answer on: the default answer skips it (its hunk ignored, the .rej
// holding the patch as given), -t assumes -R, and -f forces it through.
TEST_F(BuiltinCommandsTest, PatchReversedWithoutTerminal) {
    MakePatchDir(root);
    const std::string patched = "1\n2\n3\n4\nfive\n6\n7\n8\n9\n10\n";
    const auto applyP1 = [&](const std::vector<std::string>& args) {
        WriteFile("/p/f", patched);
        return RunCaptured("patch", args, kP1, "/p");
    };

    Captured result = applyP1({});
    EXPECT_EQ(result.out,
        "patching file f\n"
        "Reversed (or previously applied) patch detected!  Assume -R? [n] \n"
        "Apply anyway? [n] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored -- saving rejects to file f.rej\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f"), patched);
    EXPECT_EQ(ReadPatchFile(root, "f.rej"), kP1);
    EXPECT_FALSE(PatchFileExists(root, "f.orig"));

    // -N skips without the questions.
    result = applyP1({"-N"});
    EXPECT_EQ(result.out,
        "patching file f\n"
        "Reversed (or previously applied) patch detected!  Skipping patch.\n"
        "1 out of 1 hunk ignored -- saving rejects to file f.rej\n");
    EXPECT_EQ(result.status, 1);

    // -t answers the question itself and swaps the patch.
    result = applyP1({"-t"});
    EXPECT_EQ(result.out,
        "patching file f\n"
        "Reversed (or previously applied) patch detected!  Assuming -R.\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    // -f looks nowhere else: the hunk just fails, and the .orig is kept.
    result = applyP1({"-f"});
    EXPECT_EQ(result.out,
        "patching file f\n"
        "Hunk #1 FAILED at 2.\n"
        "1 out of 1 hunk FAILED -- saving rejects to file f.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f"), patched);
    EXPECT_EQ(ReadPatchFile(root, "f.orig"), patched);

    // -R on the patched file is the patch the file wants.
    result = applyP1({"-R"});
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    // -R -t on the unpatched file: the reversal shows up the other way
    // round, -R is ignored, and the patch applies as it is.
    WriteFile("/p/f", Seq(10));
    result = RunCaptured("patch", {"-R", "-t"}, kP1, "/p");
    EXPECT_EQ(result.out,
        "patching file f\n"
        "Unreversed patch detected!  Ignoring -R.\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), patched);

    // A second hunk after a batch swap of the first: the .rej holds the
    // swapped second hunk.
    std::string u = Seq(20);
    u.replace(2, 1, "TWO");
    WriteFile("/p/u", u);
    result = RunCaptured("patch", {"-t"},
        "--- u\n"
        "+++ u\n"
        "@@ -1,3 +1,3 @@\n"
        " 1\n"
        "-2\n"
        "+TWO\n"
        " 3\n"
        "@@ -14,3 +14,3 @@\n"
        " 14\n"
        "-15\n"
        "+FIFTEEN\n"
        " 16\n", "/p");
    EXPECT_EQ(result.out,
        "patching file u\n"
        "Reversed (or previously applied) patch detected!  Assuming -R.\n"
        "Hunk #2 FAILED at 14.\n"
        "1 out of 2 hunks FAILED -- saving rejects to file u.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "u"), Seq(20));
    EXPECT_EQ(ReadPatchFile(root, "u.rej"),
        "--- u\n"
        "+++ u\n"
        "@@ -14,3 +14,3 @@\n"
        " 14\n"
        "-FIFTEEN\n"
        "+15\n"
        " 16\n");
}

// A hunk that matches nowhere is reported and saved to a .rej file, the
// applied hunks still applied.
TEST_F(BuiltinCommandsTest, PatchFailedHunkWritesRej) {
    MakePatchDir(root);
    const std::string xLines = "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\n";
    WriteFile("/p/x", xLines);
    const auto xPatch =
        "--- x\t2024-01-02 03:04:05.000000000 +0000\n"
        "+++ x\t2024-01-03 03:04:05.000000000 +0000\n"
        "@@ -1,3 +1,3 @@\n"
        " a\n"
        "-b\n"
        "+B\n"
        " c\n"
        "@@ -10,3 +10,3 @@\n"
        " j\n"
        "-Q\n"
        "+K\n"
        " l\n";
    const Captured result = RunCaptured("patch", {}, xPatch, "/p");
    EXPECT_EQ(result.out,
        "patching file x\n"
        "Hunk #2 FAILED at 10.\n"
        "1 out of 2 hunks FAILED -- saving rejects to file x.rej\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "x"), "a\nB\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\n");
    EXPECT_EQ(ReadPatchFile(root, "x.orig"), xLines);
    EXPECT_EQ(ReadPatchFile(root, "x.rej"),
        "--- x\t2024-01-02 03:04:05.000000000 +0000\n"
        "+++ x\t2024-01-03 03:04:05.000000000 +0000\n"
        "@@ -10,3 +10,3 @@\n"
        " j\n"
        "-Q\n"
        "+K\n"
        " l\n");

    // The same patch the other way round, checking only: nothing is written,
    // the summary names no .rej.
    const Captured dry = RunCaptured("patch", {"--dry-run", "-R"}, xPatch, "/p");
    EXPECT_EQ(dry.out,
        "checking file x\n"
        "Hunk #2 FAILED at 10.\n"
        "1 out of 2 hunks FAILED\n");
    EXPECT_EQ(dry.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "x"), "a\nB\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\n");
    EXPECT_EQ(ReadPatchFile(root, "x.rej"),
        "--- x\t2024-01-02 03:04:05.000000000 +0000\n"
        "+++ x\t2024-01-03 03:04:05.000000000 +0000\n"
        "@@ -10,3 +10,3 @@\n"
        " j\n"
        "-Q\n"
        "+K\n"
        " l\n");

    // The verified .rej of a reversed run whose two hunks both fail.
    WriteFile("/p/t", "1\n2\n3\n4\n");
    const Captured reversed = RunCaptured("patch", {"-R", "-f"},
        "--- t\t2024-01-01 00:00:00 +0000\n"
        "+++ t\t2024-01-02 00:00:00 +0000\n"
        "@@ -1,4 +1,3 @@ fn\n"
        " 1\n"
        "-2\n"
        "-3\n"
        "+TWO\n"
        " 4\n"
        "@@ -0,0 +5,2 @@\n"
        "+p\n"
        "+q\n", "/p");
    EXPECT_EQ(reversed.out,
        "patching file t\n"
        "Hunk #1 FAILED at 1.\n"
        "Hunk #2 FAILED at 5.\n"
        "2 out of 2 hunks FAILED -- saving rejects to file t.rej\n");
    EXPECT_EQ(reversed.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "t"), "1\n2\n3\n4\n");
    EXPECT_EQ(ReadPatchFile(root, "t.rej"),
        "--- t\t2024-01-02 00:00:00 +0000\n"
        "+++ t\t2024-01-01 00:00:00 +0000\n"
        "@@ -1,3 +1,4 @@ fn\n"
        " 1\n"
        "-TWO\n"
        "+2\n"
        "+3\n"
        " 4\n"
        "@@ -5,2 +0,0 @@\n"
        "-p\n"
        "-q\n");

    // A rejected hunk's no-newline line is saved as it is, with no "\ No
    // newline" line after it.
    WriteFile("/p/n1", "a\nb\n");
    const Captured noNew1 = RunCaptured("patch", {},
        "--- n1\n"
        "+++ n1\n"
        "@@ -1,2 +1,2 @@\n"
        " a\n"
        "-b\n"
        "\\ No newline at end of file\n"
        "+B\n", "/p");
    EXPECT_EQ(noNew1.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "n1.rej"),
        "--- n1\n"
        "+++ n1\n"
        "@@ -1,2 +1,2 @@\n"
        " a\n"
        "-b+B\n");
    WriteFile("/p/n2", "a\nx\n");
    const Captured noNew2 = RunCaptured("patch", {},
        "--- n2\n"
        "+++ n2\n"
        "@@ -1,2 +1,2 @@\n"
        " a\n"
        "-b\n"
        "+B\n"
        "\\ No newline at end of file\n", "/p");
    EXPECT_EQ(noNew2.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "n2.rej"),
        "--- n2\n"
        "+++ n2\n"
        "@@ -1,2 +1,2 @@\n"
        " a\n"
        "-b\n"
        "+B");

    // Two file patches of one file, both failing: the second's rejects are
    // appended to the first's, and a stale .rej is replaced.
    WriteFile("/p/f", "1\n");
    WriteFile("/p/f.rej", "STALE\n");
    const Captured both = RunCaptured("patch", {},
        "--- f\n"
        "+++ f\n"
        "@@ -5,3 +5,3 @@\n"
        " x\n"
        "-y\n"
        "+Y\n"
        " z\n"
        "--- f\n"
        "+++ f\n"
        "@@ -6,3 +6,3 @@\n"
        " x\n"
        "-w\n"
        "+W\n"
        " z\n", "/p");
    EXPECT_EQ(both.out,
        "patching file f\n"
        "Hunk #1 FAILED at 5.\n"
        "1 out of 1 hunk FAILED -- saving rejects to file f.rej\n"
        "patching file f\n"
        "Hunk #1 FAILED at 6.\n"
        "1 out of 1 hunk FAILED -- saving rejects to file f.rej\n");
    EXPECT_EQ(both.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f.rej"),
        "--- f\n"
        "+++ f\n"
        "@@ -5,3 +5,3 @@\n"
        " x\n"
        "-y\n"
        "+Y\n"
        " z\n"
        "--- f\n"
        "+++ f\n"
        "@@ -6,3 +6,3 @@\n"
        " x\n"
        "-w\n"
        "+W\n"
        " z\n");

    // Two file patches both at an offset: one .orig, holding the content
    // before the first.
    WriteFile("/p/f", "x\n1\n2\n");
    const Captured offsets = RunCaptured("patch", {},
        "--- f\n"
        "+++ f\n"
        "@@ -1 +1 @@\n"
        "-1\n"
        "+one\n"
        "--- f\n"
        "+++ f\n"
        "@@ -1 +1 @@\n"
        "-2\n"
        "+two\n", "/p");
    EXPECT_EQ(offsets.out,
        "patching file f\n"
        "Hunk #1 succeeded at 2 (offset 1 line).\n"
        "patching file f\n"
        "Hunk #1 succeeded at 3 (offset 2 lines).\n");
    EXPECT_EQ(offsets.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), "x\none\ntwo\n");
    EXPECT_EQ(ReadPatchFile(root, "f.orig"), "x\n1\n2\n");
}

// Creation from /dev/null, deletion to /dev/null, and the conflicts of a
// patch that wants a file the wrong way round.
TEST_F(BuiltinCommandsTest, PatchCreatesAndDeletes) {
    MakePatchDir(root);
    const std::string treePatch =
        "diff -ruN a/keep b/keep\n"
        "--- a/keep\t2024-01-01 00:00:00 +0000\n"
        "+++ b/keep\t2024-01-01 00:00:00 +0000\n"
        "@@ -1 +1 @@\n"
        "-k\n"
        "+k2\n"
        "diff -ruN a/new b/new\n"
        "--- a/new\t1970-01-01 00:00:00.000000000 +0000\n"
        "+++ b/new\t2024-01-01 00:00:00 +0000\n"
        "@@ -0,0 +1 @@\n"
        "+new\n"
        "diff -ruN a/old b/old\n"
        "--- a/old\t2024-01-01 00:00:00 +0000\n"
        "+++ b/old\t1970-01-01 00:00:00.000000000 +0000\n"
        "@@ -1 +0,0 @@\n"
        "-gone\n";

    // The tree patch on a directory holding keep and old: new is created,
    // old is removed.
    WriteFile("/p/keep", "k\n");
    WriteFile("/p/old", "gone\n");
    Captured result = RunCaptured("patch", {"-p1"}, treePatch, "/p");
    EXPECT_EQ(result.out,
        "patching file keep\n"
        "patching file new\n"
        "patching file old\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "keep"), "k2\n");
    EXPECT_EQ(ReadPatchFile(root, "new"), "new\n");
    EXPECT_FALSE(PatchFileExists(root, "old"));

    // The same patch again: keep reads as already applied, new as a
    // creation against an existing file, old as a deletion of a missing one.
    result = RunCaptured("patch", {"-p1"}, treePatch, "/p");
    EXPECT_EQ(result.out,
        "patching file keep\n"
        "Reversed (or previously applied) patch detected!  Assume -R? [n] \n"
        "Apply anyway? [n] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored -- saving rejects to file keep.rej\n"
        "The next patch would create the file new,\n"
        "which already exists!  Assume -R? [n] \n"
        "Apply anyway? [n] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n"
        "The next patch would delete the file old,\n"
        "which does not exist!  Assume -R? [n] \n"
        "Apply anyway? [n] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "keep.rej"),
        "--- keep\t2024-01-01 00:00:00 +0000\n"
        "+++ keep\t2024-01-01 00:00:00 +0000\n"
        "@@ -1 +1 @@\n"
        "-k\n"
        "+k2\n");
    EXPECT_FALSE(PatchFileExists(root, "new.rej"));
    EXPECT_FALSE(PatchFileExists(root, "old.rej"));

    // -N answers the two conflicts with a skip on the same line.
    result = RunCaptured("patch", {"-p1", "-N"}, treePatch, "/p");
    EXPECT_EQ(result.out,
        "patching file keep\n"
        "Reversed (or previously applied) patch detected!  Skipping patch.\n"
        "1 out of 1 hunk ignored -- saving rejects to file keep.rej\n"
        "The next patch would create the file new,\n"
        "which already exists!  Skipping patch.\n"
        "1 out of 1 hunk ignored\n"
        "The next patch would delete the file old,\n"
        "which does not exist!  Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.status, 1);

    // -f answers them the other way: the patch has its way, and its hunks
    // fail against the file they find.
    WriteFile("/p/keep", "k\n");
    result = RunCaptured("patch", {"-p1", "-f"}, treePatch, "/p");
    EXPECT_EQ(result.out,
        "patching file keep\n"
        "The next patch would create the file new,\n"
        "which already exists!  Applying it anyway.\n"
        "patching file new\n"
        "Hunk #1 FAILED at 1.\n"
        "1 out of 1 hunk FAILED -- saving rejects to file new.rej\n"
        "The next patch would delete the file old,\n"
        "which does not exist!  Applying it anyway.\n"
        "patching file old\n"
        "Hunk #1 FAILED at 1.\n"
        "1 out of 1 hunk FAILED -- saving rejects to file old.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_FALSE(PatchFileExists(root, "old"));
    EXPECT_EQ(ReadPatchFile(root, "old.orig"), "");
    EXPECT_EQ(ReadPatchFile(root, "old.rej"),
        "--- old\t2024-01-01 00:00:00 +0000\n"
        "+++ old\t1970-01-01 00:00:00.000000000 +0000\n"
        "@@ -1 +0,0 @@\n"
        "-gone\n");

    // A creation fills an empty file; a deletion of an empty one is a
    // conflict; -R turns each into its opposite.
    WriteFile("/p/f", "");
    result = RunCaptured("patch", {},
        "--- f\t1970-01-01 00:00:00 +0000\n"
        "+++ f\t2024-01-01 00:00:00 +0000\n"
        "@@ -0,0 +1 @@\n"
        "+new\n", "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), "new\n");

    WriteFile("/p/f", "");
    result = RunCaptured("patch", {},
        "--- f\t2024-01-01 00:00:00 +0000\n"
        "+++ f\t1970-01-01 00:00:00 +0000\n"
        "@@ -1 +0,0 @@\n"
        "-old\n", "/p");
    EXPECT_EQ(result.out,
        "The next patch would empty out the file f,\n"
        "which is already empty!  Assume -R? [n] \n"
        "Apply anyway? [n] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.status, 1);

    WriteFile("/p/f", "content\n");
    result = RunCaptured("patch", {"-R"},
        "--- f\t2024-01-01 00:00:00 +0000\n"
        "+++ f\t1970-01-01 00:00:00 +0000\n"
        "@@ -1 +0,0 @@\n"
        "-old\n", "/p");
    EXPECT_EQ(result.out,
        "The next patch, when reversed, would create the file f,\n"
        "which already exists!  Ignore -R? [n] \n"
        "Apply anyway? [n] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.status, 1);

    ASSERT_EQ(root->RemoveFile("/p/f"), 0);
    result = RunCaptured("patch", {"-R"},
        "--- f\t1970-01-01 00:00:00 +0000\n"
        "+++ f\t2024-01-01 00:00:00 +0000\n"
        "@@ -0,0 +1 @@\n"
        "+new\n", "/p");
    EXPECT_EQ(result.out,
        "The next patch, when reversed, would delete the file f,\n"
        "which does not exist!  Ignore -R? [n] \n"
        "Apply anyway? [n] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.status, 1);
}

// A time in 1970 means an absent side only within the window around the
// epoch; outside it, the side is a file that is there.
TEST_F(BuiltinCommandsTest, PatchEpochWindow) {
    MakePatchDir(root);
    const auto creation = [&](const std::string& oldTime) {
        WriteFile("/p/new", "old\n");
        return RunCaptured("patch", {},
            "--- new\t" + oldTime + "\n"
            "+++ new\t2024-01-01 00:00:00 +0000\n"
            "@@ -0,0 +1 @@\n"
            "+new\n", "/p");
    };

    // Each of these counts as the epoch: the creation is refused.
    for (const char* time : {"1970-01-01 00:00:00.000000000 +0000",
                             "1969-12-31 16:00:00 -0800",
                             "1970-01-02 01:59:59 +0000",
                             "1969-12-30 23:00:01 +0000",
                             "1970-01-01",
                             "Thu Jan  1 00:00:00 1970"}) {
        const Captured result = creation(time);
        EXPECT_EQ(result.out,
            "The next patch would create the file new,\n"
            "which already exists!  Assume -R? [n] \n"
            "Apply anyway? [n] \n"
            "Skipping patch.\n"
            "1 out of 1 hunk ignored\n") << time;
        EXPECT_EQ(result.status, 1) << time;
        EXPECT_EQ(ReadPatchFile(root, "new"), "old\n") << time;
    }

    // Outside the window the old side is a file: the insertion goes in at
    // the top.
    for (const char* time : {"1970-01-02 02:00:00 +0000",
                             "1969-12-30 23:00:00 +0000",
                             "2024-01-01 00:00:00 +0000"}) {
        const Captured result = creation(time);
        EXPECT_EQ(result.out, "patching file new\n") << time;
        EXPECT_EQ(result.status, 0) << time;
        EXPECT_EQ(ReadPatchFile(root, "new"), "new\nold\n") << time;
    }
}

// A git diff's headers: index lines, new/deleted file mode, and the names
// of the sides.
TEST_F(BuiltinCommandsTest, PatchGitDiff) {
    MakePatchDir(root);
    const std::string gitPatch =
        "diff --git a/keep b/keep\n"
        "index 814f4a4..1c7f1bd 100644\n"
        "--- a/keep\n"
        "+++ b/keep\n"
        "@@ -1,2 +1,2 @@\n"
        " one\n"
        "-two\n"
        "+TWO\n"
        "diff --git a/old b/old\n"
        "deleted file mode 100644\n"
        "index 1fd0b46..0000000\n"
        "--- a/old\n"
        "+++ /dev/null\n"
        "@@ -1 +0,0 @@\n"
        "-gone\n"
        "diff --git a/new b/new\n"
        "new file mode 100644\n"
        "index 0000000..3e75765\n"
        "--- /dev/null\n"
        "+++ b/new\n"
        "@@ -0,0 +1 @@\n"
        "+new\n";
    const std::string keepLines = "one\ntwo\n";

    WriteFile("/p/keep", keepLines);
    WriteFile("/p/old", "gone\n");
    Captured result = RunCaptured("patch", {"-p1"}, gitPatch, "/p");
    EXPECT_EQ(result.out,
        "patching file keep\n"
        "patching file old\n"
        "patching file new\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "keep"), "one\nTWO\n");
    EXPECT_EQ(ReadPatchFile(root, "new"), "new\n");
    EXPECT_FALSE(PatchFileExists(root, "old"));

    // Back again with -R.
    result = RunCaptured("patch", {"-R", "-p1"}, gitPatch, "/p");
    EXPECT_EQ(result.out,
        "patching file keep\n"
        "patching file old\n"
        "patching file new\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "keep"), keepLines);
    EXPECT_EQ(ReadPatchFile(root, "old"), "gone\n");
    EXPECT_FALSE(PatchFileExists(root, "new"));

    // Without -p the last component is the name, the same as -p1 here.
    WriteFile("/p/keep", keepLines);
    WriteFile("/p/old", "gone\n");
    result = RunCaptured("patch", {}, gitPatch, "/p");
    EXPECT_EQ(result.out,
        "patching file keep\n"
        "patching file old\n"
        "patching file new\n");
    EXPECT_EQ(result.status, 0);

    // With -p0 in an empty directory the first patch names no file, the
    // second's deletion is asked about, and the third lands on b/new.
    WriteFile("/p/keep", keepLines);
    WriteFile("/p/old", "gone\n");
    result = RunCaptured("patch", {"-p0"}, gitPatch, "/p");
    EXPECT_EQ(result.out,
        "can't find file to patch at input line 5\n"
        "Perhaps you used the wrong -p or --strip option?\n"
        "The text leading up to this was:\n"
        "--------------------------\n"
        "|diff --git a/keep b/keep\n"
        "|index 814f4a4..1c7f1bd 100644\n"
        "|--- a/keep\n"
        "|+++ b/keep\n"
        "--------------------------\n"
        "File to patch: \n"
        "Skip this patch? [y] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n"
        "The next patch would delete the file a/old,\n"
        "which does not exist!  Assume -R? [n] \n"
        "Apply anyway? [n] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n"
        "patching file b/new\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "keep"), keepLines);
    EXPECT_EQ(ReadPatchFile(root, "b/new"), "new\n");

    // With -t the questions answer themselves. The file the -p0 run above
    // created goes first, so the creation below is not a conflict.
    WriteFile("/p/keep", keepLines);
    WriteFile("/p/old", "gone\n");
    ASSERT_EQ(root->RemoveFile("/p/b/new"), 0);
    result = RunCaptured("patch", {"-p0", "-t"}, gitPatch, "/p");
    EXPECT_EQ(result.out,
        "can't find file to patch at input line 5\n"
        "Perhaps you used the wrong -p or --strip option?\n"
        "The text leading up to this was:\n"
        "--------------------------\n"
        "|diff --git a/keep b/keep\n"
        "|index 814f4a4..1c7f1bd 100644\n"
        "|--- a/keep\n"
        "|+++ b/keep\n"
        "--------------------------\n"
        "No file to patch.  Skipping patch.\n"
        "1 out of 1 hunk ignored\n"
        "The next patch would delete the file a/old,\n"
        "which does not exist!  Assuming -R.\n"
        "patching file a/old\n"
        "patching file b/new\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "a/old"), "gone\n");
}

// A git patch without hunks: the creation or deletion of an empty file, a
// rename, a copy, a mode change.
TEST_F(BuiltinCommandsTest, PatchGitWithoutHunks) {
    MakePatchDir(root);
    // A deletion of an empty file removes it; a creation makes it, empty.
    WriteFile("/p/e", "");
    Captured result = RunCaptured("patch", {"-p1"},
        "diff --git a/e b/e\n"
        "deleted file mode 100644\n"
        "index e69de29..0000000\n", "/p");
    EXPECT_EQ(result.out, "patching file e\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_FALSE(PatchFileExists(root, "e"));

    result = RunCaptured("patch", {"-p1"},
        "diff --git a/n b/n\n"
        "new file mode 100644\n"
        "index 0000000..e69de29\n", "/p");
    EXPECT_EQ(result.out, "patching file n\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "n"), "");

    // The same again, on the file it now finds there.
    result = RunCaptured("patch", {"-p1"},
        "diff --git a/n b/n\n"
        "new file mode 100644\n"
        "index 0000000..e69de29\n", "/p");
    EXPECT_EQ(result.out,
        "The next patch would empty out the file n,\n"
        "which is already empty!  Assume -R? [n] \n"
        "Apply anyway? [n] \n"
        "Skipping patch.\n");
    EXPECT_EQ(result.status, 1);

    // A rename moves the file; one already renamed says so.
    WriteFile("/p/k", "content\n");
    result = RunCaptured("patch", {"-p1"},
        "diff --git a/k b/k2\n"
        "similarity index 100%\n"
        "rename from k\n"
        "rename to k2\n", "/p");
    EXPECT_EQ(result.out, "patching file k2 (renamed from k)\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_FALSE(PatchFileExists(root, "k"));
    EXPECT_EQ(ReadPatchFile(root, "k2"), "content\n");

    WriteFile("/p/k2", "content\n");
    result = RunCaptured("patch", {"-p1", "--dry-run"},
        "diff --git a/k b/k2\n"
        "similarity index 100%\n"
        "rename from k\n"
        "rename to k2\n", "/p");
    EXPECT_EQ(result.out, "checking file k2 (already renamed from k)\n");
    EXPECT_EQ(result.status, 0);

    // A copy leaves both; one without its source is refused.
    WriteFile("/p/k", "content\n");
    result = RunCaptured("patch", {"-p1"},
        "diff --git a/k b/k3\n"
        "similarity index 100%\n"
        "copy from k\n"
        "copy to k3\n", "/p");
    EXPECT_EQ(result.out, "patching file k3 (copied from k)\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "k"), "content\n");
    EXPECT_EQ(ReadPatchFile(root, "k3"), "content\n");

    result = RunCaptured("patch", {"-p1"},
        "diff --git a/missing b/k4\n"
        "similarity index 100%\n"
        "copy from missing\n"
        "copy to k4\n", "/p");
    EXPECT_EQ(result.out, "Cannot copy file without two valid file names\n");
    EXPECT_EQ(result.status, 1);

    // A mode change alone touches nothing.
    WriteFile("/p/k", "content\n");
    result = RunCaptured("patch", {"-p1"},
        "diff --git a/k b/k\n"
        "old mode 100644\n"
        "new mode 100755\n", "/p");
    EXPECT_EQ(result.out, "patching file k\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "k"), "content\n");
}

// A git rename patch with hunks: read the old name, write the new one.
TEST_F(BuiltinCommandsTest, PatchRenameFromGit) {
    MakePatchDir(root);
    WriteFile("/p/keep", "one\ntwo\n");
    const Captured result = RunCaptured("patch", {"-p1"},
        "diff --git a/keep b/kept\n"
        "similarity index 50%\n"
        "rename from keep\n"
        "rename to kept\n"
        "--- a/keep\n"
        "+++ b/kept\n"
        "@@ -1,2 +1,2 @@\n"
        " one\n"
        "-two\n"
        "+TWO\n", "/p");
    EXPECT_EQ(result.out, "patching file kept (renamed from keep)\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 0);
    EXPECT_FALSE(PatchFileExists(root, "keep"));
    EXPECT_EQ(ReadPatchFile(root, "kept"), "one\nTWO\n");

    // With -o the result goes to that file and the old one stays, the
    // rename still named (GNU patch 2.7.6: the same).
    WriteFile("/p/keep", "one\ntwo\n");
    const Captured toOut = RunCaptured("patch", {"-p1", "-o", "out"},
        "diff --git a/keep b/kept2\n"
        "similarity index 50%\n"
        "rename from keep\n"
        "rename to kept2\n"
        "--- a/keep\n"
        "+++ b/kept2\n"
        "@@ -1,2 +1,2 @@\n"
        " one\n"
        "-two\n"
        "+TWO\n", "/p");
    EXPECT_EQ(toOut.out, "patching file out (renamed from keep)\n");
    EXPECT_EQ(toOut.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "keep"), "one\ntwo\n");
    EXPECT_EQ(ReadPatchFile(root, "out"), "one\nTWO\n");
    EXPECT_FALSE(PatchFileExists(root, "kept2"));
}

// How the patch's file names are read: stripping by -p, quoting, Index:, and
// the text leading up to a patch that names no file.
TEST_F(BuiltinCommandsTest, PatchNames) {
    MakePatchDir(root);
    ASSERT_EQ(root->CreateDirectory("/p/d", kDirMode), 0);
    ASSERT_EQ(root->CreateDirectory("/p/d/e", kDirMode), 0);
    WriteFile("/p/d/e/f", "a\n");
    WriteFile("/p/d/f", "a\n");
    WriteFile("/p/f", "a\n");
    WriteFile("/p/sp ace", "a\n");
    WriteFile("/p/q\"t", "a\n");

    const std::string names =
        "--- d/e/f\n"
        "+++ d/f\n"
        "@@ -1 +1 @@\n"
        "-a\n"
        "+A\n";
    EXPECT_EQ(RunCaptured("patch", {"--dry-run"}, names, "/p").out,
        "checking file f\n");
    EXPECT_EQ(RunCaptured("patch", {"--dry-run", "-p0"}, names, "/p").out,
        "checking file d/f\n");
    EXPECT_EQ(RunCaptured("patch", {"--dry-run", "-p1"}, names, "/p").out,
        "checking file f\n");
    EXPECT_EQ(RunCaptured("patch", {"--dry-run", "-p2"}, names, "/p").out,
        "checking file f\n");
    const Captured p3 = RunCaptured("patch", {"--dry-run", "-p3"}, names, "/p");
    EXPECT_EQ(p3.out,
        "can't find file to patch at input line 3\n"
        "Perhaps you used the wrong -p or --strip option?\n"
        "The text leading up to this was:\n"
        "--------------------------\n"
        "|--- d/e/f\n"
        "|+++ d/f\n"
        "--------------------------\n"
        "File to patch: \n"
        "Skip this patch? [y] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(p3.status, 1);

    EXPECT_EQ(RunCaptured("patch", {"--dry-run", "-p2"},
        "--- /x//d/e/f\n"
        "+++ /x/d/f\n"
        "@@ -1 +1 @@\n"
        "-a\n"
        "+A\n", "/p").out,
        "checking file d/f\n");

    EXPECT_EQ(RunCaptured("patch", {"--dry-run"},
        "--- sp ace\t2024-01-01 00:00:00 +0000\n"
        "+++ sp ace\t2024-01-01 00:00:00 +0000\n"
        "@@ -1 +1 @@\n"
        "-a\n"
        "+A\n", "/p").out,
        "checking file 'sp ace'\n");
    const Captured bareSpace = RunCaptured("patch", {"--dry-run"},
        "--- sp ace\n"
        "+++ sp ace\n"
        "@@ -1 +1 @@\n"
        "-a\n"
        "+A\n", "/p");
    EXPECT_EQ(bareSpace.out,
        "can't find file to patch at input line 3\n"
        "Perhaps you should have used the -p or --strip option?\n"
        "The text leading up to this was:\n"
        "--------------------------\n"
        "|--- sp ace\n"
        "|+++ sp ace\n"
        "--------------------------\n"
        "File to patch: \n"
        "Skip this patch? [y] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(bareSpace.status, 1);
    EXPECT_EQ(RunCaptured("patch", {"--dry-run"},
        "--- \"sp ace\"\n"
        "+++ \"sp ace\"\n"
        "@@ -1 +1 @@\n"
        "-a\n"
        "+A\n", "/p").out,
        "checking file 'sp ace'\n");
    EXPECT_EQ(RunCaptured("patch", {"--dry-run"},
        "--- \"q\\\"t\"\n"
        "+++ \"q\\\"t\"\n"
        "@@ -1 +1 @@\n"
        "-a\n"
        "+A\n", "/p").out,
        "checking file 'q\"t'\n");

    const Captured nosuch = RunCaptured("patch", {"--dry-run"},
        "Index: d/f\n"
        "--- nosuch1\n"
        "+++ nosuch2\n"
        "@@ -1 +1 @@\n"
        "-a\n"
        "+A\n", "/p");
    EXPECT_EQ(nosuch.out,
        "can't find file to patch at input line 4\n"
        "Perhaps you should have used the -p or --strip option?\n"
        "The text leading up to this was:\n"
        "--------------------------\n"
        "|Index: d/f\n"
        "|--- nosuch1\n"
        "|+++ nosuch2\n"
        "--------------------------\n"
        "File to patch: \n"
        "Skip this patch? [y] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(nosuch.status, 1);
    EXPECT_EQ(RunCaptured("patch", {"--dry-run"},
        "Index: d/f\n"
        "--- /dev/null\n"
        "+++ /dev/null\n"
        "@@ -1 +1 @@\n"
        "-a\n"
        "+A\n", "/p").out,
        "checking file d/f\n");

    // Text before and between patches: leading text of the patch that names
    // no file, the first file patched first.
    WriteFile("/p/f", "a\n");
    const Captured lead = RunCaptured("patch", {"-t"},
        "header text\n"
        "--- f\n"
        "+++ f\n"
        "@@ -1 +1 @@\n"
        "-a\n"
        "+A\n"
        "trailing words\n"
        "more words\n"
        "--- nosuch\n"
        "+++ nosuch\n"
        "@@ -1 +1 @@\n"
        "-a\n"
        "+A\n", "/p");
    EXPECT_EQ(lead.out,
        "patching file f\n"
        "can't find file to patch at input line 11\n"
        "Perhaps you should have used the -p or --strip option?\n"
        "The text leading up to this was:\n"
        "--------------------------\n"
        "|trailing words\n"
        "|more words\n"
        "|--- nosuch\n"
        "|+++ nosuch\n"
        "--------------------------\n"
        "No file to patch.  Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(lead.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f"), "A\n");
}

// -o writes every result to one file, -E removes an emptied file, -d changes
// the directory, and an ORIGFILE operand overrides the headers.
TEST_F(BuiltinCommandsTest, PatchOptions) {
    MakePatchDir(root);
    const std::string treePatch =
        "diff -ruN a/keep b/keep\n"
        "--- a/keep\t2024-01-01 00:00:00 +0000\n"
        "+++ b/keep\t2024-01-01 00:00:00 +0000\n"
        "@@ -1 +1,2 @@\n"
        " one\n"
        "+TWO\n"
        "diff -ruN a/new b/new\n"
        "--- a/new\t1970-01-01 00:00:00.000000000 +0000\n"
        "+++ b/new\t2024-01-01 00:00:00 +0000\n"
        "@@ -0,0 +1 @@\n"
        "+new\n"
        "diff -ruN a/old b/old\n"
        "--- a/old\t2024-01-01 00:00:00 +0000\n"
        "+++ b/old\t1970-01-01 00:00:00.000000000 +0000\n"
        "@@ -1 +0,0 @@\n"
        "-gone\n";
    WriteFile("/p/keep", "one\n");
    WriteFile("/p/old", "gone\n");
    WritePatchFile(root, "tree.diff", treePatch);
    const Captured collected = RunCaptured(
        "patch", {"-o", "out.txt", "-i", "tree.diff", "-p1"}, "", "/p");
    EXPECT_EQ(collected.out,
        "patching file out.txt (read from keep)\n"
        "patching file out.txt (read from new)\n"
        "patching file out.txt (read from old)\n");
    EXPECT_EQ(collected.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "out.txt"), "one\nTWO\nnew\n");
    EXPECT_EQ(ReadPatchFile(root, "keep"), "one\n");
    EXPECT_EQ(ReadPatchFile(root, "old"), "gone\n");
    EXPECT_FALSE(PatchFileExists(root, "keep.orig"));

    // A failing hunk under -o: the rejects go to the output file's .rej, and
    // the output holds the unpatched input.
    WriteFile("/p/f", "a\n");
    const Captured ofail = RunCaptured("patch", {"-o", "out"},
        "--- f\n"
        "+++ f\n"
        "@@ -1 +1 @@\n"
        "-x\n"
        "+X\n", "/p");
    EXPECT_EQ(ofail.out,
        "patching file out (read from f)\n"
        "Hunk #1 FAILED at 1.\n"
        "1 out of 1 hunk FAILED -- saving rejects to file out.rej\n");
    EXPECT_EQ(ofail.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "out"), "a\n");
    EXPECT_EQ(ReadPatchFile(root, "out.rej"),
        "--- f\n"
        "+++ f\n"
        "@@ -1 +1 @@\n"
        "-x\n"
        "+X\n");
    EXPECT_FALSE(PatchFileExists(root, "f.orig"));

    // An offset under -o makes no .orig either.
    WriteFile("/p/f", "a\nb\n");
    const Captured ooff = RunCaptured("patch", {"-o", "out2"},
        "--- f\n"
        "+++ f\n"
        "@@ -3,2 +3,2 @@\n"
        " a\n"
        "-b\n"
        "+B\n", "/p");
    EXPECT_EQ(ooff.out,
        "patching file out2 (read from f)\n"
        "Hunk #1 succeeded at 1 (offset -2 lines).\n");
    EXPECT_EQ(ooff.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "out2"), "a\nB\n");
    EXPECT_FALSE(PatchFileExists(root, "f.orig"));
    EXPECT_FALSE(PatchFileExists(root, "out2.orig"));

    // -E removes a file the patch empties; without it the file stays, empty.
    WriteFile("/p/old2", "gone\n");
    const Captured withE = RunCaptured("patch", {"-E"},
        "--- old2\n"
        "+++ old2\n"
        "@@ -1 +0,0 @@\n"
        "-gone\n", "/p");
    EXPECT_EQ(withE.out, "patching file old2\n");
    EXPECT_EQ(withE.status, 0);
    EXPECT_FALSE(PatchFileExists(root, "old2"));
    WriteFile("/p/old3", "gone\n");
    const Captured withoutE = RunCaptured("patch", {},
        "--- old3\n"
        "+++ old3\n"
        "@@ -1 +0,0 @@\n"
        "-gone\n", "/p");
    EXPECT_EQ(withoutE.out, "patching file old3\n");
    EXPECT_EQ(withoutE.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "old3"), "");

    // A deletion whose result differs from the patch's own content is
    // written but not deleted.
    WriteFile("/p/g", "gone\nextra\n");
    const Captured nd = RunCaptured("patch", {},
        "--- g\t2024-01-01 00:00:00 +0000\n"
        "+++ g\t1970-01-01 00:00:00 +0000\n"
        "@@ -1 +0,0 @@\n"
        "-gone\n", "/p");
    EXPECT_EQ(nd.out,
        "patching file g\n"
        "Not deleting file g as content differs from patch\n");
    EXPECT_EQ(nd.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "g"), "extra\n");

    // -d applies everything below another directory.
    ASSERT_EQ(root->CreateDirectory("/p/sub", kDirMode), 0);
    WriteFile("/p/sub/f", "a\n");
    const Captured sub = RunCaptured("patch", {"-d", "sub"},
        "--- f\n"
        "+++ f\n"
        "@@ -1 +1 @@\n"
        "-a\n"
        "+A\n", "/p");
    EXPECT_EQ(sub.out, "patching file f\n");
    EXPECT_EQ(sub.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "sub/f"), "A\n");

    // An ORIGFILE operand overrides whatever names the patch carries.
    WriteFile("/p/x", "1\n");
    WritePatchFile(root, "p.diff",
        "--- nosuch\n"
        "+++ alsosuch\n"
        "@@ -1 +1 @@\n"
        "-1\n"
        "+one\n");
    const Captured orig = RunCaptured("patch", {"x", "p.diff"}, "", "/p");
    EXPECT_EQ(orig.out, "patching file x\n");
    EXPECT_EQ(orig.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "x"), "one\n");

    // A PATCHFILE operand wins over -i FILE, before or after it.
    WriteFile("/p/y", "1\n");
    WritePatchFile(root, "other.diff", "--- y\n+++ y\n@@ -1 +1 @@\n-1\n+other\n");
    const Captured operandWins = RunCaptured("patch", {"-i", "other.diff", "y", "p.diff"}, "", "/p");
    EXPECT_EQ(operandWins.out, "patching file y\n");
    EXPECT_EQ(operandWins.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "y"), "one\n");

    // A `~` in a name is not quoted, and the rejects file is named after it.
    WriteFile("/p/w~", "a\n");
    const Captured tilde = RunCaptured("patch", {},
        "--- w~\n"
        "+++ w~\n"
        "@@ -1 +1 @@\n"
        "-x\n"
        "+X\n", "/p");
    EXPECT_EQ(tilde.out,
        "patching file w~\n"
        "Hunk #1 FAILED at 1.\n"
        "1 out of 1 hunk FAILED -- saving rejects to file w~.rej\n");
    EXPECT_EQ(tilde.status, 1);
}

// -s keeps the questions and summaries and drops everything it can.
TEST_F(BuiltinCommandsTest, PatchSilent) {
    MakePatchDir(root);

    // No file: the text block and the questions stay, the skip line goes.
    Captured result = RunCaptured("patch", {"-s", "-p0"}, kP1, "/p");
    EXPECT_EQ(result.out,
        "The text leading up to this was:\n"
        "--------------------------\n"
        "|--- f\n"
        "|+++ g\n"
        "--------------------------\n"
        "File to patch: \n"
        "Skip this patch? [y] \n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.status, 1);

    result = RunCaptured("patch", {"-s", "-t", "-p0"}, kP1, "/p");
    EXPECT_EQ(result.out,
        "The text leading up to this was:\n"
        "--------------------------\n"
        "|--- f\n"
        "|+++ g\n"
        "--------------------------\n"
        "No file to patch.  Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.status, 1);

    // A conflict keeps its two lines and its questions.
    WriteFile("/p/new", "old\n");
    result = RunCaptured("patch", {"-s"},
        "--- new\t1970-01-01 00:00:00 +0000\n"
        "+++ new\t2024-01-01 00:00:00 +0000\n"
        "@@ -0,0 +1 @@\n"
        "+new\n", "/p");
    EXPECT_EQ(result.out,
        "The next patch would create the file new,\n"
        "which already exists!  Assume -R? [n] \n"
        "Apply anyway? [n] \n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.status, 1);

    // Success, an offset and a Not deleting say nothing at all.
    WriteFile("/p/f", Seq(10));
    result = RunCaptured("patch", {"-s", "-p0"}, kP1, "/p");
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), "1\n2\n3\n4\nfive\n6\n7\n8\n9\n10\n");

    WriteFile("/p/f", "x\ny\n" + Seq(10));
    result = RunCaptured("patch", {"-s"}, kP1, "/p");
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f.orig"), "x\ny\n" + Seq(10));

    WriteFile("/p/g", "gone\nextra\n");
    result = RunCaptured("patch", {"-s"},
        "--- g\t2024-01-01 00:00:00 +0000\n"
        "+++ g\t1970-01-01 00:00:00 +0000\n"
        "@@ -1 +0,0 @@\n"
        "-gone\n", "/p");
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "g"), "extra\n");

    // A reversed patch keeps its questions and its summary.
    WriteFile("/p/f", "1\n2\n3\n4\nfive\n6\n7\n8\n9\n10\n");
    result = RunCaptured("patch", {"-s", "-p0"}, kP1, "/p");
    EXPECT_EQ(result.out,
        "Reversed (or previously applied) patch detected!  Assume -R? [n] \n"
        "Apply anyway? [n] \n"
        "1 out of 1 hunk ignored -- saving rejects to file f.rej\n");
    EXPECT_EQ(result.status, 1);

    result = RunCaptured("patch", {"-s", "-N", "-p0"}, kP1, "/p");
    EXPECT_EQ(result.out,
        "Reversed (or previously applied) patch detected!  Skipping patch.\n"
        "1 out of 1 hunk ignored -- saving rejects to file f.rej\n");
    EXPECT_EQ(result.status, 1);

    result = RunCaptured("patch", {"-s", "-t", "-p0"}, kP1, "/p");
    EXPECT_EQ(result.out,
        "Reversed (or previously applied) patch detected!  Assuming -R.\n");
    EXPECT_EQ(result.status, 0);

    // A failed hunk: only the summary.
    WriteFile("/p/x", "a\nb\n");
    result = RunCaptured("patch", {"-s"},
        "--- x\n"
        "+++ x\n"
        "@@ -1 +1 @@\n"
        "-a\n"
        "+A\n"
        "@@ -2 +2 @@\n"
        "-z\n"
        "+Z\n", "/p");
    EXPECT_EQ(result.out,
        "1 out of 2 hunks FAILED -- saving rejects to file x.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "x"), "A\nb\n");
}

// Line endings: an LF patch on a CRLF file fails saying why, a CRLF patch is
// stripped unless --binary, and a missing final newline is carried.
TEST_F(BuiltinCommandsTest, PatchLineEndings) {
    MakePatchDir(root);

    // An LF patch on a CRLF file fails, saying why.
    WriteFile("/p/f", "a\r\nb\r\nc\r\n");
    Captured result = RunCaptured("patch", {},
        "--- f\n"
        "+++ f\n"
        "@@ -1,3 +1,3 @@\n"
        " a\n"
        "-b\n"
        "+B\n"
        " c\n", "/p");
    EXPECT_EQ(result.out,
        "patching file f\n"
        "Hunk #1 FAILED at 1 (different line endings).\n"
        "1 out of 1 hunk FAILED -- saving rejects to file f.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f"), "a\r\nb\r\nc\r\n");

    // A CRLF patch is stripped of its CRs and applies to an LF file.
    WriteFile("/p/f", "a\nb\nc\n");
    result = RunCaptured("patch", {},
        "--- f\r\n"
        "+++ f\r\n"
        "@@ -1,3 +1,3 @@\r\n"
        " a\r\n"
        "-b\r\n"
        "+B\r\n"
        " c\r\n", "/p");
    EXPECT_EQ(result.out,
        "(Stripping trailing CRs from patch; use --binary to disable.)\n"
        "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), "a\nB\nc\n");

    // On the CRLF file it still fails, the CRs gone from both sides.
    WriteFile("/p/f", "a\r\nb\r\nc\r\n");
    result = RunCaptured("patch", {},
        "--- f\r\n"
        "+++ f\r\n"
        "@@ -1,3 +1,3 @@\r\n"
        " a\r\n"
        "-b\r\n"
        "+B\r\n"
        " c\r\n", "/p");
    EXPECT_EQ(result.out,
        "(Stripping trailing CRs from patch; use --binary to disable.)\n"
        "patching file f\n"
        "Hunk #1 FAILED at 1 (different line endings).\n"
        "1 out of 1 hunk FAILED -- saving rejects to file f.rej\n");
    EXPECT_EQ(result.status, 1);

    // --binary keeps the CRs, and the patch applies to the CRLF file.
    WriteFile("/p/f", "a\r\nb\r\nc\r\n");
    result = RunCaptured("patch", {"--binary"},
        "--- f\r\n"
        "+++ f\r\n"
        "@@ -1,3 +1,3 @@\r\n"
        " a\r\n"
        "-b\r\n"
        "+B\r\n"
        " c\r\n", "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), "a\r\nB\r\nc\r\n");

    // A no-newline old line: the new line takes its place, with one.
    WriteFile("/p/n", "a\nb");
    result = RunCaptured("patch", {},
        "--- n\n"
        "+++ n\n"
        "@@ -1,2 +1,2 @@\n"
        " a\n"
        "-b\n"
        "\\ No newline at end of file\n"
        "+B\n", "/p");
    EXPECT_EQ(result.out, "patching file n\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "n"), "a\nB\n");

    // The other way round: -R of a patch whose new side lacks the newline
    // puts it back.
    WriteFile("/p/n", "a\nB");
    result = RunCaptured("patch", {"-R"},
        "--- n\n"
        "+++ n\n"
        "@@ -1,2 +1,2 @@\n"
        " a\n"
        "-b\n"
        "+B\n"
        "\\ No newline at end of file\n", "/p");
    EXPECT_EQ(result.out, "patching file n\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "n"), "a\nb\n");
}

// A malformed hunk is fatal when it is reached; one cut short by the end of
// the input may be completed; text after the last patch is ignored.
TEST_F(BuiltinCommandsTest, PatchMalformed) {
    MakePatchDir(root);

    // The messages of the hunks before it come first; the file is not
    // written.
    WriteFile("/p/t", "x\n1\nq\nr\n");
    Captured result = RunCaptured("patch", {},
        "--- t\n"
        "+++ t\n"
        "@@ -1,3 +1,3 @@\n"
        " 1\n"
        "-q\n"
        "+Q\n"
        " r\n"
        "@@ -10,3 +10,3 @@\n"
        " 10\n"
        "xx\n", "/p");
    EXPECT_EQ(result.out,
        "patching file t\n"
        "Hunk #1 succeeded at 2 (offset 1 line).\n");
    EXPECT_EQ(result.err, "patch: **** malformed patch at line 10: xx\n\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "t"), "x\n1\nq\nr\n");

    // A second file's malformed hunk: the first file stays patched. The
    // second's "patching file" line comes first too, then its fatal.
    WriteFile("/p/a", "1\n");
    WriteFile("/p/b", "q\n");
    result = RunCaptured("patch", {},
        "--- a\n"
        "+++ a\n"
        "@@ -1 +1 @@\n"
        "-1\n"
        "+one\n"
        "--- b\n"
        "+++ b\n"
        "@@ -1,2 +1,2 @@\n"
        "-q\n", "/p");
    EXPECT_EQ(result.out,
        "patching file a\n"
        "patching file b\n");
    EXPECT_EQ(result.err, "patch: **** malformed patch at line 9:  \n\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "a"), "one\n");
    EXPECT_EQ(ReadPatchFile(root, "b"), "q\n");

    // Cut short at the end of the input, the counts met on neither side
    // differently: malformed.
    WriteFile("/p/q1", "q\nx\n");
    result = RunCaptured("patch", {},
        "--- q1\n"
        "+++ q1\n"
        "@@ -1,2 +1,2 @@\n"
        "-q\n", "/p");
    EXPECT_EQ(result.out, "patching file q1\n");
    EXPECT_EQ(result.err, "patch: **** malformed patch at line 4:  \n\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "q1"), "q\nx\n");

    // The same number short on both sides, with a change line: completed
    // with empty context lines and applied.
    WriteFile("/p/q2", "q\n\n\n");
    result = RunCaptured("patch", {},
        "--- q2\n"
        "+++ q2\n"
        "@@ -1,3 +1,3 @@\n"
        "-q\n"
        "+Q\n", "/p");
    EXPECT_EQ(result.out, "patching file q2\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "q2"), "Q\n\n\n");

    // Without a change line, malformed.
    WriteFile("/p/q3", "q\n\n\n");
    result = RunCaptured("patch", {},
        "--- q3\n"
        "+++ q3\n"
        "@@ -1,3 +1,3 @@\n"
        " a\n", "/p");
    EXPECT_EQ(result.out, "patching file q3\n");
    EXPECT_EQ(result.err, "patch: **** malformed patch at line 4:  \n\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "q3"), "q\n\n\n");

    // Text after the last patch is ignored.
    WriteFile("/p/f", "a\n");
    result = RunCaptured("patch", {},
        "--- f\n"
        "+++ f\n"
        "@@ -1 +1 @@\n"
        "-a\n"
        "+A\n"
        "zz\n", "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), "A\n");
}

// Usage errors and the arguments that are accepted but not acted on.
TEST_F(BuiltinCommandsTest, PatchErrors) {
    MakePatchDir(root);

    const Captured bogus = RunCaptured("patch", {"--bogus"}, "");
    EXPECT_EQ(bogus.err,
        "patch: unrecognized option '--bogus'\n"
        "patch: Try 'patch --help' for more information.\n");
    EXPECT_EQ(bogus.status, 2);

    const Captured badShort = RunCaptured("patch", {"-U"}, "");
    EXPECT_EQ(badShort.err,
        "patch: invalid option -- 'U'\n"
        "patch: Try 'patch --help' for more information.\n");
    EXPECT_EQ(badShort.status, 2);

    const Captured notNumber = RunCaptured("patch", {"-p", "x"}, "");
    EXPECT_EQ(notNumber.err, "patch: **** strip count x is not a number\n");
    EXPECT_EQ(notNumber.status, 2);

    const Captured negative = RunCaptured("patch", {"-p", "-1"}, "");
    EXPECT_EQ(negative.err, "patch: **** strip count -1 is negative\n");
    EXPECT_EQ(negative.status, 2);

    const Captured extra = RunCaptured("patch", {"a", "b", "c"}, "");
    EXPECT_EQ(extra.err,
        "patch: c: extra operand\n"
        "patch: Try 'patch --help' for more information.\n");
    EXPECT_EQ(extra.status, 2);

    const Captured noPatchFile = RunCaptured("patch", {"-i", "nosuch"}, "");
    EXPECT_EQ(noPatchFile.err,
        "patch: **** Can't open patch file nosuch : No such file or directory\n");
    EXPECT_EQ(noPatchFile.status, 2);

    const Captured noDirectory = RunCaptured("patch", {"-d", "nodir"}, "");
    EXPECT_EQ(noDirectory.err,
        "patch: **** Can't change to directory nodir : No such file or directory\n");
    EXPECT_EQ(noDirectory.status, 2);

    const Captured garbage = RunCaptured("patch", {}, "garbage\n", "/p");
    EXPECT_EQ(garbage.err, "patch: **** Only garbage was found in the patch input.\n");
    EXPECT_EQ(garbage.status, 2);

    const Captured empty = RunCaptured("patch", {}, "", "/p");
    EXPECT_EQ(empty.out, "");
    EXPECT_EQ(empty.status, 0);

    const Captured version = RunCaptured("patch", {"-v"}, "");
    EXPECT_EQ(version.out, "patch (HaisosOS builtin) 1.1.0\n");
    EXPECT_EQ(version.status, 0);

    const Captured untreated = RunCaptured("patch", {"-m", "-x", "1", "--verbose"}, "", "/p");
    EXPECT_EQ(untreated.err,
        "Parameter -m is not treated by HaisosOS patch v. 1.1.0\n"
        "Parameter -x is not treated by HaisosOS patch v. 1.1.0\n"
        "Parameter --verbose is not treated by HaisosOS patch v. 1.1.0\n");
    EXPECT_EQ(untreated.out, "");
    EXPECT_EQ(untreated.status, 0);
}

// A hunk at the very start with no leading context is anchored to line 1:
// GNU's -F0 behaviour, the fixed starting point. With the default fuzz the
// trailing context gives instead: the hunk is found one line down.
TEST_F(BuiltinCommandsTest, PatchHunkAtStartNeedsStart) {
    MakePatchDir(root);
    WriteFile("/p/x", "z\na\nb\n");
    const Captured result = RunCaptured("patch", {},
        "--- x\n"
        "+++ x\n"
        "@@ -1,2 +1,2 @@\n"
        "-a\n"
        "+A\n"
        " b\n", "/p");
    EXPECT_EQ(result.out,
        "patching file x\n"
        "Hunk #1 succeeded at 2 with fuzz 1 (offset 1 line).\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "x"), "z\nA\nb\n");
    EXPECT_EQ(ReadPatchFile(root, "x.orig"), "z\na\nb\n");

    WriteFile("/p/x", "z\na\nb\n");
    const Captured strict = RunCaptured("patch", {"-F0"},
        "--- x\n"
        "+++ x\n"
        "@@ -1,2 +1,2 @@\n"
        "-a\n"
        "+A\n"
        " b\n", "/p");
    EXPECT_EQ(strict.out,
        "patching file x\n"
        "Hunk #1 FAILED at 1.\n"
        "1 out of 1 hunk FAILED -- saving rejects to file x.rej\n");
    EXPECT_EQ(strict.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "x"), "z\na\nb\n");
    EXPECT_EQ(ReadPatchFile(root, "x.rej"),
        "--- x\n"
        "+++ x\n"
        "@@ -1,2 +1,2 @@\n"
        "-a\n"
        "+A\n"
        " b\n");
}

// A diff the diff builtin makes, applied, leaves the two files equal.
TEST_F(BuiltinCommandsTest, PatchRoundTripWithDiff) {
    MakePatchDir(root);
    std::string aText;
    std::string bText;
    for (size_t i = 1; i <= 30; ++i) {
        const std::string line = "line " + std::to_string(i) + "\n";
        aText += line;
        bText += (i == 5 || i == 25) ? "line " + std::to_string(i) + " changed\n" : line;
    }
    bText += "line 31\n";
    WriteFile("/p/a.cpp", aText);
    WriteFile("/p/b.cpp", bText);
    const Captured result = RunCaptured("hsh",
        {"-c", "/bin/diff -u a.cpp b.cpp > p.diff; /bin/patch -p0 < p.diff; "
               "/bin/diff a.cpp b.cpp; echo $?"},
        std::nullopt, "/p");
    EXPECT_EQ(result.out,
        "patching file a.cpp\n"
        "0\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "a.cpp"), bText);
}

} // namespace Haisos