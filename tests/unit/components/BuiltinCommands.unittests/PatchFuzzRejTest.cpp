#include "PatchTestHelpers.h"

#include <initializer_list>
#include <utility>

namespace Haisos {
namespace {

// What `diff -c f g` writes for kP1's change: the context form of the same
// five -> five patch.
constexpr char kC[] =
    "*** f\tTue Jan  2 03:04:05 2024\n"
    "--- g\tTue Jan  2 03:04:05 2024\n"
    "***************\n"
    "*** 2,8 ****\n"
    "  2\n"
    "  3\n"
    "  4\n"
    "! 5\n"
    "  6\n"
    "  7\n"
    "  8\n"
    "--- 2,8 ----\n"
    "  2\n"
    "  3\n"
    "  4\n"
    "! five\n"
    "  6\n"
    "  7\n"
    "  8\n";

// The normal diff of the same change.
constexpr char kN[] = "5c5\n< 5\n---\n> five\n";

// A context-format creation of `new`, as `diff -c /dev/null new` writes it.
constexpr char kCreation[] =
    "*** /dev/null\tThu Oct  8 21:24:34 2026\n"
    "--- new\tTue Jan  2 03:04:05 2024\n"
    "***************\n"
    "*** 0 ****\n"
    "--- 1,3 ----\n"
    "+ a\n"
    "+ B\n"
    "+ c\n";

// The lines given, each with a newline after it.
std::string LinesOf(std::initializer_list<const char*> texts) {
    std::string out;
    for (const char* text : texts) {
        out += text;
        out += '\n';
    }
    return out;
}

// Seq(count) with line |number| changed to |text|.
std::string SeqWith(size_t count, size_t number, const std::string& text) {
    std::string out;
    for (size_t i = 1; i <= count; ++i) {
        out += i == number ? text : std::to_string(i);
        out += '\n';
    }
    return out;
}

// The one-hunk failure and its rejects summary, the plan's FAILED(L).
std::string FailedAt(long line) {
    return "patching file f\nHunk #1 FAILED at " + std::to_string(line)
        + ".\n1 out of 1 hunk FAILED -- saving rejects to file f.rej\n";
}

// The reversed-patch question with its assumed no, the patch saved to f.rej.
std::string ReversedQuestion() {
    return "Reversed (or previously applied) patch detected!  Assume -R? [n] \n"
           "Apply anyway? [n] \n"
           "Skipping patch.\n"
           "1 out of 1 hunk ignored -- saving rejects to file f.rej\n";
}

// A refused backup-method word: the block GNU patch prints, without the Try line.
std::string BadBackupWord(const std::string& kind, const std::string& word,
                          const std::string& option) {
    return "patch: " + kind + " argument '" + word + "' for '" + option + "'\n"
           "Valid arguments are:\n"
           "  - 'none', 'off'\n"
           "  - 'simple', 'never'\n"
           "  - 'existing', 'nil'\n"
           "  - 'numbered', 't'\n";
}

// The plan's unbalanced-context hunk: the change of line 15 to X, with |a|
// leading and |b| trailing context lines, for a file of Seq(30).
std::string UnbalancedHunk(int a, int b) {
    const int start = 15 - a;
    std::string out = "--- f\n+++ f\n@@ -" + std::to_string(start) + ","
        + std::to_string(a + b + 1) + " +" + std::to_string(start) + ","
        + std::to_string(a + b + 1) + " @@\n";
    for (int line = start; line <= 14; ++line) {
        out += " " + std::to_string(line) + "\n";
    }
    out += "-15\n+X\n";
    for (int line = 16; line <= 15 + b; ++line) {
        out += " " + std::to_string(line) + "\n";
    }
    return out;
}

// Ten lines of X, the file the reject tests let every hunk fail on.
std::string TenX() {
    std::string out;
    for (int i = 0; i < 10; ++i) {
        out += "X\n";
    }
    return out;
}

} // namespace

// The fuzz factor: how many context lines a side may spoil before the hunk
// stops matching, and the messages each level prints.
TEST_F(BuiltinCommandsTest, PatchFuzzLevels) {
    MakePatchDir(root);
    const auto reset = [&](const std::string& content) {
        WriteFile("/p/f", content);
        root->RemoveFile("/p/f.orig");
        root->RemoveFile("/p/f.rej");
    };
    const auto run = [&](const std::vector<std::string>& args) {
        return RunCaptured("patch", args, kP1, "/p");
    };

    // A spoiled leading context line costs fuzz 1; -F0 forbids it.
    reset(SeqWith(10, 2, "TWO"));
    Captured result = run({});
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 2 with fuzz 1.\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 0);
    result = run({"-F0"});
    EXPECT_EQ(result.out, FailedAt(2));
    EXPECT_EQ(result.status, 1);

    // Line 3 is two levels deep (fuzz 2), and the mismatch backup keeps
    // the input under the default rules.
    const std::string three = SeqWith(10, 3, "three");
    reset(three);
    result = run({});
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 2 with fuzz 2.\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"),
        LinesOf({"1", "2", "three", "4", "five", "6", "7", "8", "9", "10"}));
    EXPECT_EQ(ReadPatchFile(root, "f.orig"), three);

    // Lines 2 and 8 spoiled: leading and trailing give way together at
    // fuzz 1; with line 3 spoiled too it takes fuzz 2.
    reset(LinesOf({"1", "TWO", "3", "4", "5", "6", "7", "EIGHT", "9", "10"}));
    result = run({});
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 2 with fuzz 1.\n");
    EXPECT_EQ(result.status, 0);
    reset(LinesOf({"1", "2", "three", "4", "5", "6", "7", "EIGHT", "9", "10"}));
    result = run({});
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 2 with fuzz 2.\n");
    EXPECT_EQ(result.status, 0);

    // Line 4 is compared at every level the default fuzz reaches; -F3
    // leaves the hunk's delete line alone on its way.
    reset(SeqWith(10, 4, "four"));
    result = run({});
    EXPECT_EQ(result.out, FailedAt(2));
    EXPECT_EQ(result.status, 1);
    result = run({"-F3"});
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 2 with fuzz 3.\n");
    EXPECT_EQ(result.status, 0);

    // A line before everything shifts the hunk one deeper.
    reset("0\n" + three);
    result = run({});
    EXPECT_EQ(result.out,
        "patching file f\nHunk #1 succeeded at 3 with fuzz 2 (offset 1 line).\n");
    EXPECT_EQ(result.status, 0);
}

// Unbalanced context anchors a hunk at the file's start or end until the
// fuzz eats the larger side down to the smaller.
TEST_F(BuiltinCommandsTest, PatchFuzzUnbalancedContext) {
    MakePatchDir(root);
    const auto reset = [&](const std::string& content) {
        WriteFile("/p/f", content);
        root->RemoveFile("/p/f.orig");
        root->RemoveFile("/p/f.rej");
    };
    const auto run = [&](int a, int b, const std::vector<std::string>& args,
                         const std::string& content = Seq(30)) {
        reset(content);
        return RunCaptured("patch", args, UnbalancedHunk(a, b), "/p");
    };

    // More leading than trailing context: the hunk matches only at the
    // file's end until the fuzz balances the sides -- fuzz 2 for (3,1),
    // 3 for (4,1), whatever -F allows beyond it.
    Captured result = run(3, 1, {"-F1"});
    EXPECT_EQ(result.out, FailedAt(12));
    EXPECT_EQ(result.status, 1);
    result = run(3, 1, {});
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 12 with fuzz 2.\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 0);
    result = run(3, 1, {"-F6"});
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 12 with fuzz 2.\n");
    EXPECT_EQ(result.status, 0);
    result = run(4, 1, {});
    EXPECT_EQ(result.out, FailedAt(11));
    EXPECT_EQ(result.status, 1);
    result = run(4, 1, {"-F3"});
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 11 with fuzz 3.\n");
    EXPECT_EQ(result.status, 0);

    // What the fuzz ignores may differ freely: a spoiled trailing line costs
    // one level more, a spoiled ignored leading line nothing.
    result = run(3, 1, {}, SeqWith(30, 16, "Z"));
    EXPECT_EQ(result.out, FailedAt(12));
    EXPECT_EQ(result.status, 1);
    result = run(3, 1, {"-F3"}, SeqWith(30, 16, "Z"));
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 12 with fuzz 3.\n");
    EXPECT_EQ(result.status, 0);
    result = run(3, 1, {}, SeqWith(30, 13, "Z"));
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 12 with fuzz 2.\n");
    EXPECT_EQ(result.status, 0);
    result = run(3, 2, {}, SeqWith(30, 17, "Z"));
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 12 with fuzz 2.\n");
    EXPECT_EQ(result.status, 0);
    result = run(3, 2, {}, SeqWith(30, 16, "Z"));
    EXPECT_EQ(result.out, FailedAt(12));
    EXPECT_EQ(result.status, 1);
    result = run(3, 2, {"-F3"}, SeqWith(30, 16, "Z"));
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 12 with fuzz 3.\n");
    EXPECT_EQ(result.status, 0);

    // More trailing than leading: the fuzz eats the trailing side first, so
    // one spoiled trailing line costs only fuzz 1.
    result = run(1, 3, {}, SeqWith(30, 18, "Z"));
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 14 with fuzz 1.\n");
    EXPECT_EQ(result.status, 0);

    // No fuzz level reaches: the hunk's ground is wider than the file.
    reset(LinesOf({"a", "b", "c", "d"}));
    result = RunCaptured("patch", {"-F5"},
        "--- f\n+++ f\n@@ -1,6 +1,6 @@\n x\n y\n-a\n+A\n b\n c\n d\n", "/p");
    EXPECT_EQ(result.out, FailedAt(1));
    EXPECT_EQ(result.status, 1);

    // Fuzz lets a hunk's trailing context run past a short file's end.
    reset(LinesOf({"1", "2", "3"}));
    result = RunCaptured("patch", {},
        "--- f\n+++ f\n@@ -1,4 +1,4 @@\n 1\n-2\n+TWO\n 3\n 4\n", "/p");
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 1 with fuzz 1.\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), LinesOf({"1", "TWO", "3"}));
}

// The reversal probe runs at every fuzz level, against the forward search:
// whichever matches at the lower level wins.
TEST_F(BuiltinCommandsTest, PatchFuzzAndReversal) {
    MakePatchDir(root);

    // The change already in the file (line 3 spoiled): the reversal is
    // found at fuzz 2 and asked about; the assumed no leaves the file
    // alone, the patch in f.rej.
    WriteFile("/p/f",
        LinesOf({"1", "2", "three", "4", "five", "6", "7", "8", "9", "10"}));
    Captured result = RunCaptured("patch", {}, kP1, "/p");
    EXPECT_EQ(result.out, "patching file f\n" + ReversedQuestion());
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f.rej"), kP1);
    EXPECT_EQ(ReadPatchFile(root, "f"),
        LinesOf({"1", "2", "three", "4", "five", "6", "7", "8", "9", "10"}));

    // -t answers yes: the patch applies reversed, at the fuzz it took.
    result = RunCaptured("patch", {"-t"}, kP1, "/p");
    EXPECT_EQ(result.out,
        "patching file f\n"
        "Reversed (or previously applied) patch detected!  Assuming -R.\n"
        "Hunk #1 succeeded at 2 with fuzz 2.\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"),
        LinesOf({"1", "2", "three", "4", "5", "6", "7", "8", "9", "10"}));

    // A forward match at fuzz 1 beats a reversal further out: no question.
    WriteFile("/p/f", LinesOf({"1", "2", "THREE", "4", "five", "6", "7", "8", "9",
                             "10", "a", "3", "4", "5", "6", "7", "d"}));
    result = RunCaptured("patch", {}, kP1, "/p");
    EXPECT_EQ(result.out,
        "patching file f\nHunk #1 succeeded at 11 with fuzz 1 (offset 9 lines).\n");
    EXPECT_EQ(result.status, 0);

    // A reversed exact match beats a forward match at any higher fuzz: the
    // question again, the file left as it was.
    WriteFile("/p/f", LinesOf({"1", "2", "3", "4", "five", "6", "7", "8", "9",
                             "10", "a", "b", "4", "5", "6", "c", "d"}));
    result = RunCaptured("patch", {}, kP1, "/p");
    EXPECT_EQ(result.out, "patching file f\n" + ReversedQuestion());
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f"), LinesOf({"1", "2", "3", "4", "five", "6",
        "7", "8", "9", "10", "a", "b", "4", "5", "6", "c", "d"}));
}

// The numbers the messages and .rej headers print are the output's: what
// the earlier hunks of the file patch added or removed shifts them.
TEST_F(BuiltinCommandsTest, PatchOutputLineNumbers) {
    MakePatchDir(root);
    const auto reset = [&](const std::string& content) {
        WriteFile("/p/f", content);
        root->RemoveFile("/p/f.orig");
        root->RemoveFile("/p/f.rej");
    };

    // A deleting hunk shifts the next one's success line back.
    reset("0\n" + Seq(20));
    Captured result = RunCaptured("patch", {"-F0"},
        "--- f\n+++ f\n"
        "@@ -1,4 +1,3 @@\n 1\n-2\n-3\n+TWO\n 4\n"
        "@@ -10,3 +9,3 @@\n 10\n-11\n+ELEVEN\n 12\n", "/p");
    EXPECT_EQ(result.out,
        "patching file f\n"
        "Hunk #1 succeeded at 2 (offset 1 line).\n"
        "Hunk #2 succeeded at 10 (offset 1 line).\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 0);

    // The failed hunk names its input place plus the shift, and its .rej
    // ranges carry the shift -- in both formats.
    const auto twoHunks = "--- f\n+++ f\n"
        "@@ -1,4 +1,3 @@\n 1\n-2\n-3\n+TWO\n 4\n"
        "@@ -10,3 +9,3 @@\n 10\n-Q\n+ELEVEN\n 12\n";
    reset("0\n" + Seq(20));
    result = RunCaptured("patch", {"-F0"}, twoHunks, "/p");
    EXPECT_EQ(result.out,
        "patching file f\n"
        "Hunk #1 succeeded at 2 (offset 1 line).\n"
        "Hunk #2 FAILED at 9.\n"
        "1 out of 2 hunks FAILED -- saving rejects to file f.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f.rej"),
        "--- f\n+++ f\n@@ -9,3 +8,3 @@\n 10\n-Q\n+ELEVEN\n 12\n");
    reset("0\n" + Seq(20));
    result = RunCaptured("patch", {"-F0", "--reject-format=context"}, twoHunks, "/p");
    EXPECT_EQ(ReadPatchFile(root, "f.rej"),
        "*** f\n--- f\n***************\n*** 9,11 ****\n  10\n! Q\n  12\n"
        "--- 8,10 ----\n  10\n! ELEVEN\n  12\n");

    // An adding hunk shifts them forward, however far the new side claims.
    reset("0\n" + Seq(20));
    result = RunCaptured("patch", {"-F0"},
        "--- f\n+++ f\n"
        "@@ -3,3 +3,5 @@\n 2\n+a\n+b\n 3\n 4\n"
        "@@ -8,3 +100,3 @@\n 10\n-11\n+ELEVEN\n 12\n", "/p");
    EXPECT_EQ(result.out,
        "patching file f\nHunk #2 succeeded at 13 (offset 3 lines).\n");
    EXPECT_EQ(result.status, 0);

    const auto addThenFail = "--- f\n+++ f\n"
        "@@ -3,3 +3,5 @@\n 2\n+a\n+b\n 3\n 4\n"
        "@@ -10,3 +100,3 @@\n 10\n-Q\n+ELEVEN\n 12\n";
    reset("0\n" + Seq(20));
    result = RunCaptured("patch", {"-F0"}, addThenFail, "/p");
    EXPECT_EQ(result.out,
        "patching file f\n"
        "Hunk #2 FAILED at 12.\n"
        "1 out of 2 hunks FAILED -- saving rejects to file f.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f.rej"),
        "--- f\n+++ f\n@@ -12,3 +102,3 @@\n 10\n-Q\n+ELEVEN\n 12\n");
    reset("0\n" + Seq(20));
    result = RunCaptured("patch", {"-F0", "--reject-format=context"}, addThenFail, "/p");
    EXPECT_EQ(ReadPatchFile(root, "f.rej"),
        "*** f\n--- f\n***************\n*** 12,14 ****\n  10\n! Q\n  12\n"
        "--- 102,104 ----\n  10\n! ELEVEN\n  12\n");

    // A hunk found with an offset shifts nothing by itself.
    reset("0\n0\n" + Seq(20));
    result = RunCaptured("patch", {"-F0"},
        "--- f\n+++ f\n"
        "@@ -1,3 +1,3 @@\n 1\n-2\n+TWO\n 3\n"
        "@@ -10,3 +10,3 @@\n 10\n-Q\n+ELEVEN\n 12\n", "/p");
    EXPECT_EQ(result.out,
        "patching file f\n"
        "Hunk #1 succeeded at 3 (offset 2 lines).\n"
        "Hunk #2 FAILED at 10.\n"
        "1 out of 2 hunks FAILED -- saving rejects to file f.rej\n");
    EXPECT_EQ(result.status, 1);

    // The plan's case F: the (3,1) hunk after a deleting hunk needs fuzz 2
    // (its end anchor), and its success line counts the deleted line.
    reset(Seq(12));
    result = RunCaptured("patch", {},
        "--- f\n+++ f\n"
        "@@ -1,4 +1,3 @@\n 1\n-2\n-3\n+TWO\n 4\n"
        "@@ -2,5 +1,5 @@\n 2\n 3\n 4\n-5\n+FIVE\n 6\n", "/p");
    EXPECT_EQ(result.out,
        "patching file f\nHunk #2 succeeded at 1 with fuzz 2.\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"),
        LinesOf({"1", "TWO", "4", "FIVE", "6", "7", "8", "9", "10", "11", "12"}));

    // Case G: a (4,1) hunk at the very start, released by -F3, names the
    // line before the file's first -- the output's line 0.
    reset(Seq(12));
    result = RunCaptured("patch", {"-F3"},
        "--- f\n+++ f\n"
        "@@ -1,4 +1,3 @@\n 1\n-2\n-3\n+TWO\n 4\n"
        "@@ -1,6 +1,6 @@\n 1\n 2\n 3\n 4\n-5\n+FIVE\n 6\n", "/p");
    EXPECT_EQ(result.out,
        "patching file f\nHunk #2 succeeded at 0 with fuzz 3.\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"),
        LinesOf({"1", "TWO", "4", "FIVE", "6", "7", "8", "9", "10", "11", "12"}));
}

// Two hunks sharing their ground: the lines the earlier hunk handled are
// not written again, a Delete on one a no-op.
TEST_F(BuiltinCommandsTest, PatchOverlappingHunks) {
    MakePatchDir(root);
    const auto reset = [&]() {
        WriteFile("/p/f", LinesOf({"a", "b", "c", "d", "e", "f", "g", "h"}));
        root->RemoveFile("/p/f.orig");
        root->RemoveFile("/p/f.rej");
    };

    // The second hunk reuses the first's trailing context line.
    reset();
    Captured result = RunCaptured("patch", {"-F0"},
        "--- f\n+++ f\n"
        "@@ -2,3 +2,3 @@\n b\n-c\n+C\n d\n"
        "@@ -3,3 +3,3 @@\n c\n-d\n+D\n e\n", "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"),
        LinesOf({"a", "b", "C", "D", "e", "f", "g", "h"}));

    // A hunk matching wholly inside the earlier hunk's ground garbles the
    // output in GNU (which says so and applies it anyway); Haisos refuses
    // it: the candidate bound keeps it out.
    reset();
    result = RunCaptured("patch", {"-F0"},
        "--- f\n+++ f\n"
        "@@ -2,3 +2,3 @@\n b\n-c\n+C\n d\n"
        "@@ -2,3 +2,3 @@\n b\n-c\n+CC\n d\n", "/p");
    EXPECT_EQ(result.out,
        "patching file f\nHunk #2 FAILED at 2.\n"
        "1 out of 2 hunks FAILED -- saving rejects to file f.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f"),
        LinesOf({"a", "b", "C", "d", "e", "f", "g", "h"}));
    EXPECT_EQ(ReadPatchFile(root, "f.rej"),
        "--- f\n+++ f\n@@ -2,3 +2,3 @@\n b\n-c\n+CC\n d\n");

    // An insertion's ground ends at its last change, so the next hunk may
    // start where the insertion did.
    reset();
    result = RunCaptured("patch", {"-F0"},
        "--- f\n+++ f\n"
        "@@ -2,3 +2,4 @@\n b\n+X\n c\n d\n"
        "@@ -2,3 +3,3 @@\n b\n-c\n+C\n d\n", "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"),
        LinesOf({"a", "b", "X", "C", "d", "e", "f", "g", "h"}));
}

// -l: blanks at a line's end and runs of blanks inside it are ignored when
// the hunk's old lines are compared with the file.
TEST_F(BuiltinCommandsTest, PatchLooseWhitespace) {
    MakePatchDir(root);
    const auto loose = [&](const char* oldText, const char* fileLine,
                           const std::vector<std::string>& args) {
        WriteFile("/p/f", std::string("a\n") + fileLine + "\nc\n");
        root->RemoveFile("/p/f.orig");
        root->RemoveFile("/p/f.rej");
        const std::string patch = std::string("--- f\n+++ f\n@@ -1,3 +1,3 @@\n a\n-")
            + oldText + "\n+NEW\n c\n";
        return RunCaptured("patch", args, patch, "/p");
    };
    const std::vector<std::pair<const char*, const char*>> applies = {
        {"b  ", "b"}, {"b", "b  "}, {"x y", "x  y"}, {"x y", "x\ty"},
        {" b", "  b"}, {"b", "b\t"}, {"", "   "},
    };
    for (const auto& [oldText, fileLine] : applies) {
        const Captured result = loose(oldText, fileLine, {"-l", "-F0"});
        EXPECT_EQ(result.out, "patching file f\n") << oldText << " vs " << fileLine;
        EXPECT_EQ(result.err, "") << oldText << " vs " << fileLine;
        EXPECT_EQ(result.status, 0) << oldText << " vs " << fileLine;
        EXPECT_EQ(ReadPatchFile(root, "f"), "a\nNEW\nc\n") << oldText;
    }
    const std::vector<std::pair<const char*, const char*>> fails = {
        {"x y", "xy"}, {"xy", "x y"}, {"b", "  b"}, {"  b", "b"},
        {"b", "B"}, {"b", "b\r"}, {"x y", "x\vy"}, {"x y", "x\fy"},
    };
    for (const auto& [oldText, fileLine] : fails) {
        const Captured result = loose(oldText, fileLine, {"-l", "-F0"});
        EXPECT_EQ(result.out, FailedAt(1)) << oldText << " vs " << fileLine;
        EXPECT_EQ(result.status, 1) << oldText << " vs " << fileLine;
    }

    // Without -l the comparison is exact.
    Captured result = loose("b", "b  ", {"-F0"});
    EXPECT_EQ(result.out, FailedAt(1));
    EXPECT_EQ(result.status, 1);

    // The file's own lines are what the context copies, so a line without
    // its newline keeps not having one.
    WriteFile("/p/f", "a\nb\nc");
    result = RunCaptured("patch", {"-l", "-F0"},
        "--- f\n+++ f\n@@ -1,3 +1,3 @@\n a\n-b\n+NEW\n c\n", "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), "a\nNEW\nc");

    // The file's context keeps its trailing blanks under -l too.
    WriteFile("/p/f", "a  \nb\nc\n");
    result = RunCaptured("patch", {"-l", "-F0"},
        "--- f\n+++ f\n@@ -1,3 +1,3 @@\n a\n-b\n+NEW\n c\n", "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), "a  \nNEW\nc\n");

    // Blanks on the hunk's side: the deleted line matches loosely.
    WriteFile("/p/f", Seq(10));
    result = RunCaptured("patch", {"-l", "-F0"},
        "--- f\n+++ f\n@@ -1,3 +1,3 @@\n 1\n-2  \n+two\n 3\n", "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"),
        LinesOf({"1", "two", "3", "4", "5", "6", "7", "8", "9", "10"}));
}

// Context diffs: `diff -c`'s format, both of its styles, read and applied.
TEST_F(BuiltinCommandsTest, PatchContextDiff) {
    MakePatchDir(root);
    const auto reset = [&](const std::string& content) {
        WriteFile("/p/f", content);
        root->RemoveFile("/p/f.orig");
        root->RemoveFile("/p/f.rej");
    };

    reset(Seq(10));
    Captured result = RunCaptured("patch", {}, kC, "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), SeqWith(10, 5, "five"));

    // -R swaps the sides: the change goes back.
    result = RunCaptured("patch", {"-R"}, kC, "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    // The located hunk is the converted one: the unified form's fuzz rules
    // apply to it unchanged.
    reset(SeqWith(10, 3, "three"));
    result = RunCaptured("patch", {}, kC, "/p");
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 2 with fuzz 2.\n");
    EXPECT_EQ(result.status, 0);
    reset(SeqWith(10, 3, "three"));
    result = RunCaptured("patch", {"-F0"}, kC, "/p");
    EXPECT_EQ(result.out, FailedAt(2));
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f.rej"), kC);
    WriteFile("/p/f", LinesOf({"1", "2", "three", "four", "5", "6", "7", "8", "9", "10"}));
    result = RunCaptured("patch", {"-F3"}, kC, "/p");
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 2 with fuzz 3.\n");
    EXPECT_EQ(result.status, 0);

    // A deletion: the new part lists no lines of its own.
    WriteFile("/p/a", Seq(10));
    result = RunCaptured("patch", {},
        "*** a\n--- del\n***************\n*** 2,8 ****\n  2\n  3\n  4\n- 5\n"
        "  6\n  7\n  8\n--- 2,7 ----\n", "/p");
    EXPECT_EQ(result.out, "patching file a\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "a"),
        LinesOf({"1", "2", "3", "4", "6", "7", "8", "9", "10"}));

    // An insertion: the old part lists no lines of its own.
    WriteFile("/p/a", Seq(10));
    result = RunCaptured("patch", {},
        "*** a\n--- ins\n***************\n*** 3,8 ****\n--- 3,9 ----\n"
        "  3\n  4\n  5\n+ new\n  6\n  7\n  8\n", "/p");
    EXPECT_EQ(result.out, "patching file a\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "a"),
        LinesOf({"1", "2", "3", "4", "5", "new", "6", "7", "8", "9", "10"}));

    // The old style: no " ****" on the old header, "-----" after the new.
    reset(Seq(10));
    result = RunCaptured("patch", {},
        "*** f\n--- f\n***************\n*** 4,6\n  4\n! 5\n  6\n"
        "--- 4,6\n  4\n! FIVE\n  6\n", "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), SeqWith(10, 5, "FIVE"));

    // A creation.
    result = RunCaptured("patch", {}, kCreation, "/p");
    EXPECT_EQ(result.out, "patching file new\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "new"), "a\nB\nc\n");

    // A "\ No newline at end of file" on the new side is kept.
    WriteFile("/p/old", "a\nb\n");
    result = RunCaptured("patch", {},
        "*** old\n--- nn\n***************\n*** 1,2 ****\n  a\n! b\n"
        "--- 1,2 ----\n  a\n! b\n\\ No newline at end of file\n", "/p");
    EXPECT_EQ(result.out, "patching file old\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "old"), "a\nb");

    // The star line: eight stars are a hunk separator, seven are not.
    std::string starPatch = kC;
    starPatch.replace(starPatch.find("***************"), 15, "********");
    reset(Seq(10));
    result = RunCaptured("patch", {}, starPatch, "/p");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), SeqWith(10, 5, "five"));
    starPatch.replace(starPatch.find("********"), 8, "*******");
    reset(Seq(10));
    result = RunCaptured("patch", {}, starPatch, "/p");
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.err, "patch: **** Only garbage was found in the patch input.\n");
    EXPECT_EQ(result.status, 2);

    // No file of the names exists: the question block with its assumed
    // answer, all of it on stdout.
    const std::string kCHunk = std::string(kC).substr(std::string(kC).find("****"));
    result = RunCaptured("patch", {}, std::string("*** nosuch\n--- nosuch2\n") + kCHunk,
                         "/p");
    EXPECT_EQ(result.out,
        "can't find file to patch at input line 3\n"
        "Perhaps you should have used the -p or --strip option?\n"
        "The text leading up to this was:\n"
        "--------------------------\n"
        "|*** nosuch\n"
        "|--- nosuch2\n"
        "--------------------------\n"
        "File to patch: \n"
        "Skip this patch? [y] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 1);
}

// A context hunk that does not add up: each way it can break, and the
// message that names where.
TEST_F(BuiltinCommandsTest, PatchContextMalformed) {
    MakePatchDir(root);
    const auto run = [&](const std::string& hunk, const std::string& content) {
        WriteFile("/p/f", content);
        root->RemoveFile("/p/f.orig");
        root->RemoveFile("/p/f.rej");
        return RunCaptured("patch", {},
            std::string("*** f\n--- f\n***************\n") + hunk, "/p");
    };

    // A mark the old part does not know.
    Captured result = run("*** 4,6 ****\n  4\nX 5\n  6\n--- 4,6 ----\n"
                          "  4\n! FIVE\n  6\n", Seq(10));
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.err, "patch: **** malformed patch at line 6: X 5\n\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    // The old part short of its stated count meets its '---' early.
    result = run("*** 4,6 ****\n  4\n! 5\n--- 4,6 ----\n  4\n! FIVE\n  6\n", Seq(10));
    EXPECT_EQ(result.err,
        "patch: **** Premature '---' at line 7; check line numbers at line 4\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    // Over it: the '---' is overdue.
    result = run("*** 4,5 ****\n  4\n! 5\n  6\n--- 4,6 ----\n  4\n! FIVE\n  6\n",
                 Seq(10));
    EXPECT_EQ(result.err,
        "patch: **** Overdue '---' at line 8; check line numbers at line 4\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    // A mark the new part does not know.
    result = run("*** 4,6 ****\n  4\n! 5\n  6\n--- 4,6 ----\n  4\nX FIVE\n  6\n",
                 Seq(10));
    EXPECT_EQ(result.err, "patch: **** malformed patch at line 10: X FIVE\n\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    // The new part listing no change where the old part had one.
    result = run("*** 4,6 ****\n  4\n! 5\n  6\n--- 4,5 ----\n  4\n  6\n", Seq(10));
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.err,
        "patch: **** Out-of-sync patch, lines 6,10 -- mangled text or line numbers, maybe?\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    // The new part short, another hunk following: the star line that would
    // start it ends this one.
    result = run("*** 4,6 ****\n  4\n! 5\n  6\n--- 4,6 ----\n  4\n! FIVE\n"
                 "***************\n*** 4,6 ****\n  4\n! 5\n  6\n"
                 "--- 4,6 ----\n  4\n! FIVE\n  6\n", Seq(10));
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.err, "patch: **** unexpected end of hunk at line 11\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    // The new part short at the input's end: completed from the old part
    // and applied.
    result = run("*** 4,6 ****\n  4\n! 5\n  6\n--- 4,6 ----\n  4\n! FIVE\n", Seq(10));
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), SeqWith(10, 5, "FIVE"));

    // A good hunk applies; the broken one after it stops the file patch
    // with the file as it was.
    result = run("*** 1,2 ****\n! 1\n  2\n--- 1,2 ----\n! ONE\n  2\n"
                 "***************\n*** 4,6 ****\n  4\nX 5\n  6\n"
                 "--- 4,6 ----\n  4\n! FIVE\n  6\n", "0\n" + Seq(10));
    EXPECT_EQ(result.out,
        "patching file f\nHunk #1 succeeded at 2 with fuzz 1 (offset 1 line).\n");
    EXPECT_EQ(result.err, "patch: **** malformed patch at line 13: X 5\n\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), "0\n" + Seq(10));
}

// Normal diffs: the `5c5` grammar, the file named by the ORIGFILE operand.
TEST_F(BuiltinCommandsTest, PatchNormalDiff) {
    MakePatchDir(root);
    const auto reset = [&](const std::string& content) {
        WriteFile("/p/f", content);
        WriteFile("/p/a", content);
        root->RemoveFile("/p/f.orig");
        root->RemoveFile("/p/f.rej");
        root->RemoveFile("/p/a.rej");
    };

    reset(Seq(10));
    Captured result = RunCaptured("patch", {"f"}, kN, "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.err, "");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), SeqWith(10, 5, "five"));

    // A normal diff names no file: without the operand it is not one.
    result = RunCaptured("patch", {}, kN, "/p");
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.err, "patch: **** Only garbage was found in the patch input.\n");
    EXPECT_EQ(result.status, 2);

    // -n reads it; with no name anywhere, the question and its answer.
    result = RunCaptured("patch", {"-n"}, kN, "/p");
    EXPECT_EQ(result.out,
        "can't find file to patch at input line 1\n"
        "File to patch: \n"
        "Skip this patch? [y] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.status, 1);

    result = RunCaptured("patch", {"-n", "-s"}, kN, "/p");
    EXPECT_EQ(result.out,
        "File to patch: \n"
        "Skip this patch? [y] \n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.status, 1);

    result = RunCaptured("patch", {"-n", "-t"}, kN, "/p");
    EXPECT_EQ(result.out,
        "can't find file to patch at input line 1\n"
        "No file to patch.  Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.status, 1);

    // Text before the hunk shows in the block; the operand names the file.
    result = RunCaptured("patch", {"-n"}, std::string("some text\nmore\n") + kN, "/p");
    EXPECT_EQ(result.out,
        "can't find file to patch at input line 3\n"
        "The text leading up to this was:\n"
        "--------------------------\n"
        "|some text\n"
        "|more\n"
        "--------------------------\n"
        "File to patch: \n"
        "Skip this patch? [y] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.status, 1);
    reset(Seq(10));
    result = RunCaptured("patch", {"-n", "f"}, std::string("some text\nmore\n") + kN, "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), SeqWith(10, 5, "five"));

    // The hunk is searched like any other.
    reset("0\n0\n" + Seq(10));
    result = RunCaptured("patch", {"f"}, kN, "/p");
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 7 (offset 2 lines).\n");
    EXPECT_EQ(result.status, 0);

    // On the patched file: the reversal question.
    reset(SeqWith(10, 5, "five"));
    result = RunCaptured("patch", {"f"}, kN, "/p");
    EXPECT_EQ(result.out, "patching file f\n" + ReversedQuestion());
    EXPECT_EQ(result.status, 1);

    // -o takes the result; the input file stays as it was.
    reset(Seq(10));
    result = RunCaptured("patch", {"-o", "out2", "f"}, kN, "/p");
    EXPECT_EQ(result.out, "patching file out2 (read from f)\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "out2"), SeqWith(10, 5, "five"));
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    // Several hunks of one file patch: a change, a deletion, an append.
    const char* kM = "3c3\n< 3\n---\n> three\n6d5\n< 6\n7a7\n> seven2\n";
    WriteFile("/p/a", LinesOf({"1", "2", "3", "4", "X", "6", "7", "8", "9", "10"}));
    root->RemoveFile("/p/a.rej");
    result = RunCaptured("patch", {"a"}, kM, "/p");
    EXPECT_EQ(result.out, "patching file a\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "a"),
        LinesOf({"1", "2", "three", "4", "X", "7", "seven2", "8", "9", "10"}));

    // The placed append lands even where the rest fails; the two failures
    // go to a.rej in the input's own (context) form.
    WriteFile("/p/a", TenX());
    root->RemoveFile("/p/a.rej");
    result = RunCaptured("patch", {"a"}, kM, "/p");
    EXPECT_EQ(result.out,
        "patching file a\n"
        "Hunk #1 FAILED at 3.\n"
        "Hunk #2 FAILED at 6.\n"
        "2 out of 3 hunks FAILED -- saving rejects to file a.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "a"),
        LinesOf({"X", "X", "X", "X", "X", "X", "X", "seven2", "X", "X", "X"}));
    EXPECT_EQ(ReadPatchFile(root, "a.rej"),
        "*** /dev/null\n--- /dev/null\n***************\n"
        "*** 3\n- 3\n--- 3 -----\n+ three\n"
        "***************\n*** 6\n- 6\n--- 0 -----\n");

    // The format options force one grammar; the others are not read.
    result = RunCaptured("patch", {"-c"}, kP1, "/p");
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.err, "patch: **** Only garbage was found in the patch input.\n");
    EXPECT_EQ(result.status, 2);
    result = RunCaptured("patch", {"-u"}, kC, "/p");
    EXPECT_EQ(result.err, "patch: **** Only garbage was found in the patch input.\n");
    EXPECT_EQ(result.status, 2);
    result = RunCaptured("patch", {"-n", "f"}, kC, "/p");
    EXPECT_EQ(result.err, "patch: **** Only garbage was found in the patch input.\n");
    EXPECT_EQ(result.status, 2);
    result = RunCaptured("patch", {"-n", "f"}, kP1, "/p");
    EXPECT_EQ(result.err, "patch: **** Only garbage was found in the patch input.\n");
    EXPECT_EQ(result.status, 2);
}

// A normal-diff hunk that does not add up.
TEST_F(BuiltinCommandsTest, PatchNormalMalformed) {
    MakePatchDir(root);
    const auto run = [&](const std::string& patch) {
        WriteFile("/p/f", Seq(10));
        root->RemoveFile("/p/f.rej");
        return RunCaptured("patch", {"f"}, patch, "/p");
    };

    Captured result = run("5c5\n< 5\nX\n> five\n");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.err, "patch: **** '---' expected at line 3 of patch\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    result = run("5,6c5\n< 5\n---\n> five\n");
    EXPECT_EQ(result.err, "patch: **** '<' followed by space or tab expected at line 3 of patch\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    result = run("5c5\n< 5\n---\nX\n");
    EXPECT_EQ(result.err, "patch: **** '>' followed by space or tab expected at line 4 of patch\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    result = run("5c5,6\n< 5\n---\n> five\n");
    EXPECT_EQ(result.err, "patch: **** unexpected end of file in patch at line 4\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    // The good hunk before the broken one does not save the file.
    result = run("2c2\n< 2\n---\n> two\n5c5\n< 5\nX\n> five\n");
    EXPECT_EQ(result.err, "patch: **** '---' expected at line 7 of patch\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    // A command line with nothing after it is no hunk at all.
    result = run("5a6\n");
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.err, "patch: **** Only garbage was found in the patch input.\n");
    EXPECT_EQ(result.status, 2);
}

// Where the rejects go (-r) and in which grammar (--reject-format).
TEST_F(BuiltinCommandsTest, PatchRejectOptions) {
    MakePatchDir(root);
    const auto reset = [&]() {
        WriteFile("/p/f", SeqWith(10, 5, "X"));
        root->RemoveFile("/p/f.rej");
        root->RemoveFile("/p/my.rej");
    };
    const auto failedSavingTo = [&](const std::string& name) {
        return "patching file f\nHunk #1 FAILED at 2.\n1 out of 1 hunk FAILED"
            " -- saving rejects to file " + name + "\n";
    };

    // -r names the reject file; the default f.rej is left alone.
    reset();
    Captured result = RunCaptured("patch", {"-r", "my.rej"}, kC, "/p");
    EXPECT_EQ(result.out, failedSavingTo("my.rej"));
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "my.rej"), kC);
    EXPECT_FALSE(PatchFileExists(root, "f.rej"));

    // -r - throws the rejects away; the summary ends after the count.
    reset();
    result = RunCaptured("patch", {"-r", "-"}, kC, "/p");
    EXPECT_EQ(result.out, "patching file f\nHunk #1 FAILED at 2.\n1 out of 1 hunk FAILED\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_FALSE(PatchFileExists(root, "f.rej"));

    reset();
    result = RunCaptured("patch", {"-r", "-", "-s"}, kC, "/p");
    EXPECT_EQ(result.out, "1 out of 1 hunk FAILED\n");
    EXPECT_EQ(result.status, 1);

    // --dry-run checks only: no reject file is written, none named.
    reset();
    result = RunCaptured("patch", {"-r", "my.rej", "--dry-run"}, kC, "/p");
    EXPECT_EQ(result.out, "checking file f\nHunk #1 FAILED at 2.\n1 out of 1 hunk FAILED\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_FALSE(PatchFileExists(root, "my.rej"));

    // On the already patched file the question, its rejects discarded.
    WriteFile("/p/f", SeqWith(10, 5, "five"));
    root->RemoveFile("/p/f.rej");
    result = RunCaptured("patch", {"-r", "-"}, kC, "/p");
    EXPECT_EQ(result.out,
        "patching file f\nReversed (or previously applied) patch detected!"
        "  Assume -R? [n] \nApply anyway? [n] \nSkipping patch.\n"
        "1 out of 1 hunk ignored\n");
    EXPECT_EQ(result.status, 1);

    // One reject file for the whole run: replaced by the first file patch's
    // rejects, appended to after the second's.
    WriteFile("/p/x", "a\n");
    WriteFile("/p/y", "b\n");
    WriteFile("/p/my.rej", "STALE\n");
    result = RunCaptured("patch", {"-r", "my.rej"},
        "--- x\n+++ x\n@@ -1 +1 @@\n-q\n+Q\n"
        "--- y\n+++ y\n@@ -1 +1 @@\n-r\n+R\n", "/p");
    EXPECT_EQ(result.out,
        "patching file x\nHunk #1 FAILED at 1.\n1 out of 1 hunk FAILED"
        " -- saving rejects to file my.rej\n"
        "patching file y\nHunk #1 FAILED at 1.\n1 out of 1 hunk FAILED"
        " -- saving rejects to file my.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "my.rej"),
        "--- x\n+++ x\n@@ -1 +1 @@\n-q\n+Q\n"
        "--- y\n+++ y\n@@ -1 +1 @@\n-r\n+R\n");

    // A reject file that cannot be created is fatal, after its summary.
    reset();
    result = RunCaptured("patch", {"-r", "nodir/my.rej"}, kC, "/p");
    EXPECT_EQ(result.out, failedSavingTo("nodir/my.rej"));
    EXPECT_EQ(result.err, "patch: **** Can't create file nodir/my.rej : No such file or directory\n");
    EXPECT_EQ(result.status, 2);

    // The other grammar, on either input: the hunk keeps its own lines,
    // the header the input's names and times.
    reset();
    result = RunCaptured("patch", {"--reject-format=context"}, kP1, "/p");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f.rej"),
        std::string("*** f\n--- g\n")
        + std::string(kC).substr(std::string(kC).find("***************")));

    reset();
    result = RunCaptured("patch", {"--reject-format=unified"}, kC, "/p");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f.rej"),
        "--- f\tTue Jan  2 03:04:05 2024\n+++ g\tTue Jan  2 03:04:05 2024\n"
        "@@ -2,7 +2,7 @@\n 2\n 3\n 4\n-5\n+five\n 6\n 7\n 8\n");

    // The format words, exactly, no prefixes.
    reset();
    result = RunCaptured("patch", {"--reject-format=foo"}, kC, "/p");
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.err, "patch: Try 'patch --help' for more information.\n");
    EXPECT_EQ(result.status, 2);
    result = RunCaptured("patch", {"--reject-format=c"}, kC, "/p");
    EXPECT_EQ(result.err, "patch: Try 'patch --help' for more information.\n");
    EXPECT_EQ(result.status, 2);

    // A reject file that is a directory cannot be written to either.
    reset();
    root->RemoveFile("/p/f.rej");
    ASSERT_EQ(root->CreateDirectory("/p/f.rej", kDirMode), 0);
    result = RunCaptured("patch", {"-r", "f.rej"}, kC, "/p");
    EXPECT_EQ(result.out, failedSavingTo("f.rej"));
    EXPECT_EQ(result.err, "patch: **** Can't create file f.rej : Is a directory\n");
    EXPECT_EQ(result.status, 2);
}

// The reject text of every input grammar, in both reject formats.
TEST_F(BuiltinCommandsTest, PatchContextRejects) {
    MakePatchDir(root);
    // `diff -c a mix` for a ten-line file with three changes: a change, a
    // deletion and an insertion.
    const char* kMixContext =
        "*** a\n--- mix\n***************\n*** 1,10 ****\n"
        "  1\n  2\n! 3\n  4\n  5\n- 6\n  7\n  8\n  9\n  10\n"
        "--- 1,10 ----\n  1\n  2\n! three\n  4\n  5\n  7\n+ seven2\n"
        "  8\n  9\n  10\n";
    const char* kMixContextHunk =
        "***************\n*** 1,10 ****\n"
        "  1\n  2\n! 3\n  4\n  5\n- 6\n  7\n  8\n  9\n  10\n"
        "--- 1,10 ----\n  1\n  2\n! three\n  4\n  5\n  7\n+ seven2\n"
        "  8\n  9\n  10\n";
    const char* kMixUnifiedHunk =
        "@@ -1,10 +1,10 @@\n 1\n 2\n-3\n+three\n 4\n 5\n-6\n 7\n"
        "+seven2\n 8\n 9\n 10\n";
    const auto startOver = [&](const char* name) {
        WriteFile(std::string("/p/") + name, TenX());
        root->RemoveFile(std::string("/p/") + name + ".rej");
    };

    // A context input's rejects are its own text again.
    startOver("a");
    Captured result = RunCaptured("patch", {}, kMixContext, "/p");
    EXPECT_EQ(result.out,
        "patching file a\nHunk #1 FAILED at 1.\n"
        "1 out of 1 hunk FAILED -- saving rejects to file a.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "a.rej"), kMixContext);

    // A unified input, its rejects asked for in the context grammar: the
    // same hunk, the header the input's names and times.
    startOver("a");
    result = RunCaptured("patch", {"--reject-format=context"},
        "--- a\t2024-01-02 03:04:05.000000000 +0000\n"
        "+++ mix\t2024-01-02 03:04:05.000000000 +0000\n" + std::string(kMixUnifiedHunk),
        "/p");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "a.rej"),
        "*** a\t2024-01-02 03:04:05.000000000 +0000\n"
        "--- mix\t2024-01-02 03:04:05.000000000 +0000\n" + std::string(kMixContextHunk));

    // A context input, its rejects asked for in the unified grammar.
    startOver("a");
    result = RunCaptured("patch", {"--reject-format=unified"}, kMixContext, "/p");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "a.rej"),
        std::string("--- a\n+++ mix\n") + kMixUnifiedHunk);

    // A normal input's rejects in the unified grammar (its context form is
    // PatchNormalDiff's); the placed hunk at the end is not rejected.
    startOver("a");
    result = RunCaptured("patch", {"--reject-format=unified", "a"},
        "3c3\n< 3\n---\n> three\n6d5\n< 6\n7a7\n> seven2\n", "/p");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "a.rej"),
        "--- /dev/null\n+++ /dev/null\n@@ -3 +3 @@\n-3\n+three\n"
        "@@ -6 +5,0 @@\n-6\n");

    // An old-style context hunk, saved in its own style.
    startOver("f");
    result = RunCaptured("patch", {},
        "*** f\n--- f\n***************\n*** 4,6\n  4\n! 5\n  6\n"
        "--- 4,6 -----\n  4\n! FIVE\n  6\n", "/p");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f.rej"),
        "*** f\n--- f\n***************\n*** 4,6\n  4\n! 5\n  6\n"
        "--- 4,6 -----\n  4\n! FIVE\n  6\n");

    // A function name on the star line stays with the hunk.
    startOver("f");
    result = RunCaptured("patch", {},
        "*** f\n--- f\n*************** fn()\n*** 4,6 ****\n  4\n! 5\n  6\n"
        "--- 4,6 ----\n  4\n! FIVE\n  6\n", "/p");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f.rej"),
        "*** f\n--- f\n*************** fn()\n*** 4,6 ****\n  4\n! 5\n  6\n"
        "--- 4,6 ----\n  4\n! FIVE\n  6\n");
    startOver("f");
    result = RunCaptured("patch", {"--reject-format=context"},
        "--- f\n+++ f\n@@ -4,3 +4,3 @@ fn()\n 4\n-5\n+FIVE\n 6\n", "/p");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f.rej"),
        "*** f\n--- f\n*************** fn()\n*** 4,6 ****\n  4\n! 5\n  6\n"
        "--- 4,6 ----\n  4\n! FIVE\n  6\n");

    // A hunk refused by -F0 is saved with the context it still has, whole
    // in both parts.
    WriteFile("/p/k", "a\nb\nc\n");
    root->RemoveFile("/p/k.rej");
    result = RunCaptured("patch", {"-F0", "--reject-format=context"},
        "--- k\n+++ k\n@@ -1,2 +1,3 @@\n Q\n+new\n b\n", "/p");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "k.rej"),
        "*** k\n--- k\n***************\n*** 1,2 ****\n  Q\n  b\n"
        "--- 1,3 ----\n  Q\n+ new\n  b\n");
    // Without -F0 the same hunk matches with fuzz and applies.
    WriteFile("/p/k", "a\nb\nc\n");
    result = RunCaptured("patch", {},
        "--- k\n+++ k\n@@ -1,2 +1,3 @@\n Q\n+new\n b\n", "/p");
    EXPECT_EQ(result.out, "patching file k\nHunk #1 succeeded at 1 with fuzz 1.\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "k"), "a\nnew\nb\nc\n");

    // A hunk rejected after earlier ones shifted the file: the rej ranges
    // are the output's line numbers, and a placed hunk between them applies.
    WriteFile("/p/f", "Q\n");
    root->RemoveFile("/p/f.rej");
    result = RunCaptured("patch", {"--reject-format=context"},
        "--- f\n+++ f\n@@ -5,0 +6 @@\n+new\n@@ -8 +8,0 @@\n-8\n"
        "@@ -10,2 +10,2 @@\n-x\n-y\n+X\n+Y\n", "/p");
    EXPECT_EQ(result.out,
        "patching file f\nHunk #2 FAILED at 9.\nHunk #3 FAILED at 11.\n"
        "2 out of 3 hunks FAILED -- saving rejects to file f.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f"), "Q\nnew\n");
    EXPECT_EQ(ReadPatchFile(root, "f.rej"),
        "*** f\n--- f\n***************\n*** 9 ****\n- 8\n--- 0 ----\n"
        "***************\n*** 11,12 ****\n! x\n! y\n--- 11,12 ----\n! X\n! Y\n");
    WriteFile("/p/f", "Q\n");
    root->RemoveFile("/p/f.rej");
    result = RunCaptured("patch", {},
        "--- f\n+++ f\n@@ -5,0 +6 @@\n+new\n@@ -8 +8,0 @@\n-8\n"
        "@@ -10,2 +10,2 @@\n-x\n-y\n+X\n+Y\n", "/p");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "f.rej"),
        "--- f\n+++ f\n@@ -9 +9,0 @@\n-8\n@@ -11,2 +11,2 @@\n-x\n-y\n+X\n+Y\n");

    // A creation forced onto a file with content: every hunk refused, the
    // file left as it is. The rej's /dev/null side carries no time.
    WriteFile("/p/new", "x\ny\n");
    root->RemoveFile("/p/new.rej");
    result = RunCaptured("patch", {"-f"},
        "--- /dev/null\t2026-10-08 21:24:34.863626600 +0300\n"
        "+++ new\t2024-01-02 03:04:05.000000000 +0200\n"
        "@@ -0,0 +1,3 @@\n+a\n+B\n+c\n", "/p");
    EXPECT_EQ(result.out,
        "The next patch would create the file new,\n"
        "which already exists!  Applying it anyway.\n"
        "patching file new\nHunk #1 FAILED at 1.\n"
        "1 out of 1 hunk FAILED -- saving rejects to file new.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "new"), "x\ny\n");
    EXPECT_EQ(ReadPatchFile(root, "new.rej"),
        "--- /dev/null\n+++ new\t2024-01-02 03:04:05.000000000 +0200\n"
        "@@ -0,0 +1,3 @@\n+a\n+B\n+c\n");
    WriteFile("/p/new", "x\ny\n");
    root->RemoveFile("/p/new.rej");
    result = RunCaptured("patch", {"-f", "--reject-format=context"},
        "--- /dev/null\t2026-10-08 21:24:34.863626600 +0300\n"
        "+++ new\t2024-01-02 03:04:05.000000000 +0200\n"
        "@@ -0,0 +1,3 @@\n+a\n+B\n+c\n", "/p");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "new.rej"),
        "*** /dev/null\n--- new\t2024-01-02 03:04:05.000000000 +0200\n"
        "***************\n*** 0 ****\n--- 1,3 ----\n+ a\n+ B\n+ c\n");
}

// The backup options: when a backup is made, where it goes, and what the
// method words and the environment decide.
TEST_F(BuiltinCommandsTest, PatchBackups) {
    MakePatchDir(root);
    const auto reset = [&]() {
        WriteFile("/p/f", Seq(10));
        for (const char* name : {"f.orig", "f.bak", "f.s", "f.~1~", "f.~2~",
                                 "f.~3~", "out", "old", "old.orig", "new",
                                 "new.orig"}) {
            root->RemoveFile(std::string("/p/") + name);
        }
    };
    const auto env = [&](std::initializer_list<std::pair<const char*, const char*>> vars) {
        auto environment = factory->CreateEnvironment();
        for (const auto& var : vars) {
            environment->SetVariable(var.first, var.second);
        }
        return environment;
    };
    const char* kTenPatch =
        "*** f\n--- f\n***************\n*** 9,10 ****\n  9\n! 10\n"
        "--- 9,10 ----\n  9\n! TEN\n";

    // -b backs up the original; -z names the backup's suffix.
    reset();
    Captured result = RunCaptured("patch", {"-b"}, kC, "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), SeqWith(10, 5, "five"));
    EXPECT_EQ(ReadPatchFile(root, "f.orig"), Seq(10));

    reset();
    result = RunCaptured("patch", {"-b", "-z", ".bak"}, kC, "/p");
    EXPECT_EQ(ReadPatchFile(root, "f.bak"), Seq(10));
    EXPECT_FALSE(PatchFileExists(root, "f.orig"));

    // Without -b a clean application makes no backup.
    reset();
    result = RunCaptured("patch", {"-z", ".bak"}, kC, "/p");
    EXPECT_EQ(result.status, 0);
    EXPECT_FALSE(PatchFileExists(root, "f.bak"));
    EXPECT_FALSE(PatchFileExists(root, "f.orig"));

    // The numbered method counts up; -R on the result counts again.
    reset();
    result = RunCaptured("patch", {"-V", "numbered", "-b"}, kC, "/p");
    EXPECT_EQ(ReadPatchFile(root, "f.~1~"), Seq(10));
    result = RunCaptured("patch", {"-V", "numbered", "-b", "-R"}, kC, "/p");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));
    EXPECT_EQ(ReadPatchFile(root, "f.~2~"), SeqWith(10, 5, "five"));
    EXPECT_EQ(ReadPatchFile(root, "f.~1~"), Seq(10));

    // The existing method numbers only where numbered backups already are.
    result = RunCaptured("patch", {"-V", "existing", "-b"}, kC, "/p");
    EXPECT_EQ(ReadPatchFile(root, "f.~3~"), Seq(10));
    reset();
    result = RunCaptured("patch", {"-V", "existing", "-b"}, kC, "/p");
    EXPECT_EQ(ReadPatchFile(root, "f.orig"), Seq(10));
    EXPECT_FALSE(PatchFileExists(root, "f.~1~"));

    // PATCH_VERSION_CONTROL over VERSION_CONTROL; an empty word is existing.
    reset();
    result = RunCaptured("patch", {"-b"}, kC, "/p",
        env({{"VERSION_CONTROL", "numbered"}}));
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f.~1~"), Seq(10));
    reset();
    result = RunCaptured("patch", {"-b"}, kC, "/p",
        env({{"PATCH_VERSION_CONTROL", "numbered"}, {"VERSION_CONTROL", "simple"}}));
    EXPECT_EQ(ReadPatchFile(root, "f.~1~"), Seq(10));
    reset();
    result = RunCaptured("patch", {"-b"}, kC, "/p",
        env({{"PATCH_VERSION_CONTROL", ""}, {"VERSION_CONTROL", "numbered"}}));
    EXPECT_EQ(ReadPatchFile(root, "f.orig"), Seq(10));

    // SIMPLE_BACKUP_SUFFIX names the suffix.
    reset();
    result = RunCaptured("patch", {"-b"}, kC, "/p",
        env({{"SIMPLE_BACKUP_SUFFIX", ".s"}}));
    EXPECT_EQ(ReadPatchFile(root, "f.s"), Seq(10));

    // A hunk applied with an offset is a mismatch: the default backs up,
    // --no-backup-if-mismatch does not, and -z/-V still name its place.
    WriteFile("/p/f", "Q\n" + Seq(10));
    for (const char* name : {"f.orig", "f.bak", "f.~1~", "f.~2~", "f.~3~", "f.s"}) {
        root->RemoveFile(std::string("/p/") + name);
    }
    result = RunCaptured("patch", {}, kP1, "/p");
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 3 (offset 1 line).\n");
    EXPECT_EQ(ReadPatchFile(root, "f.orig"), "Q\n" + Seq(10));
    WriteFile("/p/f", "Q\n" + Seq(10));
    root->RemoveFile("/p/f.orig");
    result = RunCaptured("patch", {"--no-backup-if-mismatch"}, kP1, "/p");
    EXPECT_EQ(result.status, 0);
    EXPECT_FALSE(PatchFileExists(root, "f.orig"));
    WriteFile("/p/f", "Q\n" + Seq(10));
    result = RunCaptured("patch", {"-z", ".bak"}, kP1, "/p");
    EXPECT_EQ(ReadPatchFile(root, "f.bak"), "Q\n" + Seq(10));
    WriteFile("/p/f", "Q\n" + Seq(10));
    root->RemoveFile("/p/f.bak");
    result = RunCaptured("patch", {"-V", "numbered"}, kP1, "/p");
    EXPECT_EQ(ReadPatchFile(root, "f.~1~"), "Q\n" + Seq(10));

    // none and off make numbered backups, as GNU 2.7.6's; never and nil are
    // the simple and existing spellings.
    reset();
    result = RunCaptured("patch", {"-b", "-V", "none"}, kC, "/p");
    EXPECT_EQ(ReadPatchFile(root, "f.~1~"), Seq(10));
    reset();
    result = RunCaptured("patch", {"-b", "-V", "off"}, kC, "/p");
    EXPECT_EQ(ReadPatchFile(root, "f.~1~"), Seq(10));
    reset();
    result = RunCaptured("patch", {"-b", "-V", "never"}, kC, "/p");
    EXPECT_EQ(ReadPatchFile(root, "f.orig"), Seq(10));
    reset();
    result = RunCaptured("patch", {"-b", "-V", "nil"}, kC, "/p");
    EXPECT_EQ(ReadPatchFile(root, "f.orig"), Seq(10));

    // --dry-run and -o make no backup; nor does a patch skipped at its
    // question.
    reset();
    result = RunCaptured("patch", {"-b", "--dry-run"}, kC, "/p");
    EXPECT_EQ(result.out, "checking file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_FALSE(PatchFileExists(root, "f.orig"));
    reset();
    result = RunCaptured("patch", {"-b", "-o", "out"}, kC, "/p");
    EXPECT_EQ(result.out, "patching file out (read from f)\n");
    EXPECT_EQ(ReadPatchFile(root, "out"), SeqWith(10, 5, "five"));
    EXPECT_FALSE(PatchFileExists(root, "f.orig"));
    EXPECT_FALSE(PatchFileExists(root, "out.orig"));
    WriteFile("/p/f", SeqWith(10, 5, "five"));
    root->RemoveFile("/p/f.orig");
    result = RunCaptured("patch", {"-b"}, kC, "/p");
    EXPECT_EQ(result.out, "patching file f\n" + ReversedQuestion());
    EXPECT_EQ(result.status, 1);
    EXPECT_FALSE(PatchFileExists(root, "f.orig"));

    // A creation's backup is an empty file.
    reset();
    result = RunCaptured("patch", {"-b"}, kCreation, "/p");
    EXPECT_EQ(result.out, "patching file new\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "new"), "a\nB\nc\n");
    EXPECT_EQ(ReadPatchFile(root, "new.orig"), "");

    // A deletion's backup holds what was deleted.
    reset();
    WriteFile("/p/old", "x\n");
    result = RunCaptured("patch", {"-b", "-p0"},
        "--- old\n+++ /dev/null\n@@ -1 +0,0 @@\n-x\n", "/p");
    EXPECT_EQ(result.out, "patching file old\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_FALSE(PatchFileExists(root, "old"));
    EXPECT_EQ(ReadPatchFile(root, "old.orig"), "x\n");

    // One backup per file per run: the first content wins.
    reset();
    result = RunCaptured("patch", {"-b", "-V", "numbered"},
        std::string(kC) + kTenPatch, "/p");
    EXPECT_EQ(result.out, "patching file f\npatching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), LinesOf(
        {"1", "2", "3", "4", "five", "6", "7", "8", "9", "TEN"}));
    EXPECT_EQ(ReadPatchFile(root, "f.~1~"), Seq(10));
    EXPECT_FALSE(PatchFileExists(root, "f.~2~"));
}

// The backup and count options' own errors: what is refused, and in what
// order, and where the method words come from.
TEST_F(BuiltinCommandsTest, PatchBackupOptionErrors) {
    MakePatchDir(root);
    const auto reset = [&]() {
        WriteFile("/p/f", Seq(10));
        for (const char* name : {"f.orig", "f.~1~"}) {
            root->RemoveFile(std::string("/p/") + name);
        }
    };
    const auto env = [&](std::initializer_list<std::pair<const char*, const char*>> vars) {
        auto environment = factory->CreateEnvironment();
        for (const auto& var : vars) {
            environment->SetVariable(var.first, var.second);
        }
        return environment;
    };
    const char* const kOptionWord = "--version-control or -V option";

    reset();
    Captured result = RunCaptured("patch", {"-F", "x"}, kC, "/p");
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.err, "patch: **** fuzz factor x is not a number\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    result = RunCaptured("patch", {"-F", "-1"}, kC, "/p");
    EXPECT_EQ(result.err, "patch: **** fuzz factor -1 is negative\n");
    EXPECT_EQ(result.status, 2);

    result = RunCaptured("patch", {"-F", "2x"}, kC, "/p");
    EXPECT_EQ(result.err, "patch: **** fuzz factor 2x is not a number\n");
    EXPECT_EQ(result.status, 2);

    result = RunCaptured("patch", {"-F", ""}, kC, "/p");
    EXPECT_EQ(result.err, "patch: **** fuzz factor '' is not a number\n");
    EXPECT_EQ(result.status, 2);

    // Digits beyond int64 saturate, so a huge fuzz factor only means none.
    result = RunCaptured("patch", {"-F", "99999999999999999999"}, kC, "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f"), SeqWith(10, 5, "five"));

    reset();
    result = RunCaptured("patch", {"-z", ""}, kC, "/p");
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.err, "patch: **** backup suffix is empty\n");
    EXPECT_EQ(result.status, 2);

    // A bad method word is refused with GNU's block, with or without -b; an
    // ambiguous prefix the same way.
    result = RunCaptured("patch", {"-b", "-V", "bogus"}, kC, "/p");
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.err, BadBackupWord("invalid", "bogus", kOptionWord));
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));
    result = RunCaptured("patch", {"-V", "bogus"}, kC, "/p");
    EXPECT_EQ(result.err, BadBackupWord("invalid", "bogus", kOptionWord));
    EXPECT_EQ(result.status, 2);
    result = RunCaptured("patch", {"-b", "-V", "n"}, kC, "/p");
    EXPECT_EQ(result.err, BadBackupWord("ambiguous", "n", kOptionWord));
    EXPECT_EQ(result.status, 2);

    // The last -V wins when every one of them is good.
    reset();
    result = RunCaptured("patch", {"-V", "bogus", "-V", "simple", "-b"}, kC, "/p");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f.orig"), Seq(10));
    reset();
    result = RunCaptured("patch", {"-V", "none", "-V", "bogus", "-b"}, kC, "/p");
    EXPECT_EQ(result.err, BadBackupWord("invalid", "bogus", kOptionWord));
    EXPECT_EQ(result.status, 2);

    // -p, -F, -z and --reject-format are checked in command-line order.
    result = RunCaptured("patch", {"-F", "x", "-z", ""}, kC, "/p");
    EXPECT_EQ(result.err, "patch: **** fuzz factor x is not a number\n");
    result = RunCaptured("patch", {"-z", "", "-F", "x"}, kC, "/p");
    EXPECT_EQ(result.err, "patch: **** backup suffix is empty\n");
    result = RunCaptured("patch", {"--reject-format=foo", "-F", "x"}, kC, "/p");
    EXPECT_EQ(result.err, "patch: Try 'patch --help' for more information.\n");
    EXPECT_EQ(result.status, 2);

    // The environment's words, read only when no -V is given.
    reset();
    result = RunCaptured("patch", {"-b"}, kC, "/p",
        env({{"VERSION_CONTROL", "bogus"}}));
    EXPECT_EQ(result.err, BadBackupWord("invalid", "bogus", "$VERSION_CONTROL"));
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));
    result = RunCaptured("patch", {}, kC, "/p",
        env({{"VERSION_CONTROL", "bogus"}}));
    EXPECT_EQ(result.err, BadBackupWord("invalid", "bogus", "$VERSION_CONTROL"));
    EXPECT_EQ(result.status, 2);
    result = RunCaptured("patch", {"-b"}, kC, "/p",
        env({{"PATCH_VERSION_CONTROL", "bogus"}}));
    EXPECT_EQ(result.err, BadBackupWord("invalid", "bogus", "$PATCH_VERSION_CONTROL"));
    EXPECT_EQ(result.status, 2);

    reset();
    result = RunCaptured("patch", {"-V", "simple", "-b"}, kC, "/p",
        env({{"VERSION_CONTROL", "bogus"}}));
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f.orig"), Seq(10));
    reset();
    result = RunCaptured("patch", {"-b"}, kC, "/p",
        env({{"PATCH_VERSION_CONTROL", "numbered"},
             {"VERSION_CONTROL", "bogus"}}));
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "f.~1~"), Seq(10));
}

// A backup or output file that cannot be made is fatal, the target left as
// it was.
TEST_F(BuiltinCommandsTest, PatchFileFailures) {
    MakePatchDir(root);
    WriteFile("/p/f", Seq(10));
    ASSERT_EQ(root->CreateDirectory("/p/f.orig", kDirMode), 0);
    Captured result = RunCaptured("patch", {"-b"}, kC, "/p");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.err, "patch: **** Can't rename file f to f.orig : Is a directory\n");
    EXPECT_EQ(result.status, 2);
    EXPECT_EQ(ReadPatchFile(root, "f"), Seq(10));

    // An offset hunk's mismatch backup meets the same directory.
    WriteFile("/p/f", "Q\n" + Seq(10));
    result = RunCaptured("patch", {}, kP1, "/p");
    EXPECT_EQ(result.out, "patching file f\nHunk #1 succeeded at 3 (offset 1 line).\n");
    EXPECT_EQ(result.err, "patch: **** Can't rename file f to f.orig : Is a directory\n");
    EXPECT_EQ(result.status, 2);

    ASSERT_EQ(root->CreateDirectory("/p/o", kDirMode), 0);
    result = RunCaptured("patch", {"-o", "o"}, kC, "/p");
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.err, "patch: **** Can't create file o : Is a directory\n");
    EXPECT_EQ(result.status, 2);

    result = RunCaptured("patch", {"-o", "nodir/out"}, kC, "/p");
    EXPECT_EQ(result.out, "");
    EXPECT_EQ(result.err, "patch: **** Can't create file nodir/out : No such file or directory\n");
    EXPECT_EQ(result.status, 2);
}

// Counts and line numbers beyond what a 64-bit integer holds.
TEST_F(BuiltinCommandsTest, PatchHugeNumbers) {
    MakePatchDir(root);
    const auto run = [&](const std::string& header) {
        WriteFile("/p/f", Seq(10));
        root->RemoveFile("/p/f.rej");
        return RunCaptured("patch", {}, "--- f\n+++ f\n" + header + "+x\n", "/p");
    };

    // An empty range whose start would overflow is malformed; a number with
    // more digits than int64 holds is too large. Both fatal, the message
    // ending in a blank line.
    Captured result = run("@@ -9223372036854775807,0 +1 @@\n");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.err,
        "patch: **** malformed patch at line 3: @@ -9223372036854775807,0 +1 @@\n\n");
    EXPECT_EQ(result.status, 2);

    result = run("@@ -1 +9223372036854775807,0 @@\n");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.err,
        "patch: **** malformed patch at line 3: @@ -1 +9223372036854775807,0 @@\n\n");
    EXPECT_EQ(result.status, 2);

    result = run("@@ -99999999999999999999 +1 @@\n");
    EXPECT_EQ(result.out, "patching file f\n");
    EXPECT_EQ(result.err,
        "patch: **** line number 99999999999999999999 is too large at line 3:"
        " @@ -99999999999999999999 +1 @@\n\n");
    EXPECT_EQ(result.status, 2);

    // A huge -p strips every component out of the name. GNU wraps the count
    // to 32 bits (stripping 0); Haisos takes it as written, and the file is
    // not found either way.
    ASSERT_EQ(root->CreateDirectory("/p/a", kDirMode), 0);
    ASSERT_EQ(root->CreateDirectory("/p/a/d", kDirMode), 0);
    WriteFile("/p/a/d/f", "1\n");
    const char* kNotFound =
        "can't find file to patch at input line 3\n"
        "Perhaps you used the wrong -p or --strip option?\n"
        "The text leading up to this was:\n"
        "--------------------------\n"
        "|--- a/d/f\n"
        "|+++ a/d/f\n"
        "--------------------------\n"
        "File to patch: \n"
        "Skip this patch? [y] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored\n";
    result = RunCaptured("patch", {"--dry-run", "-p", "99999999999999999999"},
        "--- a/d/f\n+++ a/d/f\n@@ -1 +1 @@\n-1\n+one\n", "/p");
    EXPECT_EQ(result.out, kNotFound);
    EXPECT_EQ(result.status, 1);
    result = RunCaptured("patch", {"--dry-run", "-p", "4294967296"},
        "--- a/d/f\n+++ a/d/f\n@@ -1 +1 @@\n-1\n+one\n", "/p");
    EXPECT_EQ(result.out, kNotFound);
    EXPECT_EQ(result.status, 1);
}

// The ORIGFILE operand with a git rename or copy: the file read, the file
// written, and which of them stays.
TEST_F(BuiltinCommandsTest, PatchGitRenameWithOrigFile) {
    MakePatchDir(root);
    const auto reset = [&]() {
        WriteFile("/p/old", "1\n2\n3\n");
        WriteFile("/p/other", "1\n2\n3\n");
        for (const char* name : {"new", "new.rej", "new.orig", "nosuch.orig", "out"}) {
            root->RemoveFile(std::string("/p/") + name);
        }
    };
    const char* kRenamePatch =
        "diff --git a/old b/new\n"
        "similarity index 80%\n"
        "rename from old\n"
        "rename to new\n"
        "--- a/old\n"
        "+++ b/new\n"
        "@@ -1,3 +1,3 @@\n 1\n-2\n+TWO\n 3\n";

    // A rename reads ORIGFILE, writes NEW and removes ORIGFILE; the patch's
    // own old name is left alone.
    reset();
    Captured result = RunCaptured("patch", {"-p1", "other"}, kRenamePatch, "/p");
    EXPECT_EQ(result.out, "patching file new (renamed from other)\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "new"), "1\nTWO\n3\n");
    EXPECT_FALSE(PatchFileExists(root, "other"));
    EXPECT_EQ(ReadPatchFile(root, "old"), "1\n2\n3\n");

    // A copy keeps ORIGFILE.
    reset();
    result = RunCaptured("patch", {"-p1", "other"},
        "diff --git a/old b/new\n"
        "similarity index 80%\n"
        "copy from old\n"
        "copy to new\n"
        "--- a/old\n"
        "+++ b/new\n"
        "@@ -1,3 +1,3 @@\n 1\n-2\n+TWO\n 3\n", "/p");
    EXPECT_EQ(result.out, "patching file new (copied from other)\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "new"), "1\nTWO\n3\n");
    EXPECT_EQ(ReadPatchFile(root, "other"), "1\n2\n3\n");

    // A rename without hunks moves ORIGFILE's content to NEW.
    reset();
    WriteFile("/p/other", "other\n");
    result = RunCaptured("patch", {"-p1", "other"},
        "diff --git a/old b/new\n"
        "similarity index 100%\n"
        "rename from old\n"
        "rename to new\n", "/p");
    EXPECT_EQ(result.out, "patching file new (renamed from other)\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "new"), "other\n");
    EXPECT_FALSE(PatchFileExists(root, "other"));

    // With -o the output goes to its file and ORIGFILE is kept.
    reset();
    result = RunCaptured("patch", {"-p1", "-o", "out", "other"}, kRenamePatch, "/p");
    EXPECT_EQ(result.out, "patching file out (renamed from other)\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "out"), "1\nTWO\n3\n");
    EXPECT_EQ(ReadPatchFile(root, "other"), "1\n2\n3\n");

    // A missing ORIGFILE: NEW is written empty, with an empty backup of it
    // (GNU also leaves an empty ORIGFILE backup there).
    reset();
    result = RunCaptured("patch", {"-p1", "nosuch"}, kRenamePatch, "/p");
    EXPECT_EQ(result.out,
        "patching file new (renamed from nosuch)\n"
        "Hunk #1 FAILED at 1.\n"
        "1 out of 1 hunk FAILED -- saving rejects to file new.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "new"), "");
    EXPECT_EQ(ReadPatchFile(root, "new.orig"), "");
    EXPECT_FALSE(PatchFileExists(root, "nosuch.orig"));
}

// A deletion whose file differs: the hunks' report comes first, the refusal
// to delete before the rejects summary.
TEST_F(BuiltinCommandsTest, PatchNotDeletingBeforeSummary) {
    MakePatchDir(root);
    WriteFile("/p/old", "x\ny\n");
    root->RemoveFile("/p/old.rej");
    Captured result = RunCaptured("patch", {"-f"},
        "*** old\n--- /dev/null\n***************\n*** 1,2 ****\n- a\n- b\n"
        "--- 0 ----\n", "/p");
    EXPECT_EQ(result.out,
        "patching file old\nHunk #1 FAILED at 1.\n"
        "Not deleting file old as content differs from patch\n"
        "1 out of 1 hunk FAILED -- saving rejects to file old.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "old"), "x\ny\n");
    EXPECT_EQ(ReadPatchFile(root, "old.rej"),
        "*** old\n--- /dev/null\n***************\n*** 1,2 ****\n- a\n- b\n"
        "--- 0 ----\n");

    // Without -f the reversed probe finds the deletion's swap and the patch
    // is skipped at its question: nothing is said of deleting.
    root->RemoveFile("/p/old.rej");
    result = RunCaptured("patch", {},
        "*** old\n--- /dev/null\n***************\n*** 1,2 ****\n- a\n- b\n"
        "--- 0 ----\n", "/p");
    EXPECT_EQ(result.out,
        "patching file old\n"
        "Reversed (or previously applied) patch detected!  Assume -R? [n] \n"
        "Apply anyway? [n] \n"
        "Skipping patch.\n"
        "1 out of 1 hunk ignored -- saving rejects to file old.rej\n");
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(ReadPatchFile(root, "old"), "x\ny\n");
}

// The backups of a git copy or rename: the file written is backed up (empty
// when it was not there); a rename's source is moved to its own backup, a
// copy's source is left alone.
TEST_F(BuiltinCommandsTest, PatchGitCopyAndRenameBackups) {
    MakePatchDir(root);
    const auto gitPatch = [](const char* how) {
        return std::string("diff --git a/old b/new\nsimilarity index 80%\n") + how
            + " from old\n" + how + " to new\n--- a/old\n+++ b/new\n"
            "@@ -1,3 +1,3 @@\n 1\n-2\n+TWO\n 3\n";
    };
    const auto reset = [&](const char* old) {
        WriteFile("/p/old", old);
        for (const char* name : {"new", "new.orig", "old.orig"}) {
            root->RemoveFile(std::string("/p/") + name);
        }
    };

    reset("1\n2\n3\n");
    Captured result = RunCaptured("patch", {"-p1", "-b"}, gitPatch("copy"), "/p");
    EXPECT_EQ(result.out, "patching file new (copied from old)\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "new"), "1\nTWO\n3\n");
    EXPECT_EQ(ReadPatchFile(root, "new.orig"), "");
    EXPECT_EQ(ReadPatchFile(root, "old"), "1\n2\n3\n");

    // A mismatch backup of a copy keeps its source too.
    reset("0\n1\n2\n3\n");
    result = RunCaptured("patch", {"-p1"}, gitPatch("copy"), "/p");
    EXPECT_EQ(result.out,
        "patching file new (copied from old)\nHunk #1 succeeded at 2 (offset 1 line).\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "new"), "0\n1\nTWO\n3\n");
    EXPECT_EQ(ReadPatchFile(root, "new.orig"), "");
    EXPECT_EQ(ReadPatchFile(root, "old"), "0\n1\n2\n3\n");

    reset("0\n1\n2\n3\n");
    result = RunCaptured("patch", {"-p1"}, gitPatch("rename"), "/p");
    EXPECT_EQ(result.out,
        "patching file new (renamed from old)\nHunk #1 succeeded at 2 (offset 1 line).\n");
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(ReadPatchFile(root, "new"), "0\n1\nTWO\n3\n");
    EXPECT_EQ(ReadPatchFile(root, "new.orig"), "");
    EXPECT_EQ(ReadPatchFile(root, "old.orig"), "0\n1\n2\n3\n");
    EXPECT_FALSE(PatchFileExists(root, "old"));
}

} // namespace Haisos