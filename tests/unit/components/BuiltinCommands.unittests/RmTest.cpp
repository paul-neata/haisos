#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"

using namespace Haisos;

namespace {

const std::string kNone;

} // namespace

// --- rm ---

TEST_F(BuiltinCommandsTest, RmRemovesAFile) {
    const auto run = RunCaptured("rm", {"/notes.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_FALSE(EntryTypeOf(*root, "/notes.txt").has_value());
}

TEST_F(BuiltinCommandsTest, RmVerbose) {
    const auto run = RunCaptured("rm", {"-v", "/notes.txt"});
    EXPECT_EQ(run.out, "removed '/notes.txt'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, RmMissingOperand) {
    const auto run = RunCaptured("rm", {});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "rm: missing operand\nTry 'rm --help' for more information.\n");
    EXPECT_EQ(run.status, 1);

    const auto forced = RunCaptured("rm", {"-f"});
    EXPECT_EQ(forced.out, kNone);
    EXPECT_EQ(forced.err, kNone);
    EXPECT_EQ(forced.status, 0);
}

TEST_F(BuiltinCommandsTest, RmMissingFile) {
    const auto run = RunCaptured("rm", {"/nothing"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "rm: cannot remove '/nothing': No such file or directory\n");
    EXPECT_EQ(run.status, 1);

    const auto forced = RunCaptured("rm", {"-f", "/nothing"});
    EXPECT_EQ(forced.out, kNone);
    EXPECT_EQ(forced.err, kNone);
    EXPECT_EQ(forced.status, 0);
}

TEST_F(BuiltinCommandsTest, RmDirectoryWithoutR) {
    const auto run = RunCaptured("rm", {"/docs"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "rm: cannot remove '/docs': Is a directory\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_EQ(EntryTypeOf(*root, "/docs"), DirectoryEntryType::Dir);
}

TEST_F(BuiltinCommandsTest, RmDirEmptyAndNot) {
    ASSERT_EQ(root->CreateDirectory("/e", kDirMode), 0);
    const auto emptied = RunCaptured("rm", {"-dv", "/e"});
    EXPECT_EQ(emptied.out, "removed directory '/e'\n");
    EXPECT_EQ(emptied.err, kNone);
    EXPECT_EQ(emptied.status, 0);
    EXPECT_FALSE(EntryTypeOf(*root, "/e").has_value());

    const auto full = RunCaptured("rm", {"-d", "/docs"});
    EXPECT_EQ(full.out, kNone);
    EXPECT_EQ(full.err, "rm: cannot remove '/docs': Directory not empty\n");
    EXPECT_EQ(full.status, 1);
    EXPECT_EQ(EntryTypeOf(*root, "/docs"), DirectoryEntryType::Dir);
}

TEST_F(BuiltinCommandsTest, RmRecursiveVerboseInNameOrder) {
    WriteFile("/docs/c.md", "charlie");
    const auto run = RunCaptured("rm", {"-rv", "/docs"});
    EXPECT_EQ(run.out,
        "removed '/docs/a.md'\n"
        "removed '/docs/c.md'\n"
        "removed '/docs/sub/b.md'\n"
        "removed directory '/docs/sub'\n"
        "removed directory '/docs'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_FALSE(EntryTypeOf(*root, "/docs").has_value());
}

TEST_F(BuiltinCommandsTest, RmRelativeFromWorkingDirectory) {
    const auto run = RunCaptured("rm", {"-v", "a.md"}, std::nullopt, "/docs");
    EXPECT_EQ(run.out, "removed 'a.md'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_FALSE(EntryTypeOf(*root, "/docs/a.md").has_value());
}

TEST_F(BuiltinCommandsTest, RmInteractiveDeclinedAndAccepted) {
    const std::string prompt = "rm: remove regular file '/notes.txt'? ";
    const auto declined = RunCaptured("rm", {"-i", "/notes.txt"}, "n\n");
    EXPECT_EQ(declined.out, kNone);
    EXPECT_EQ(declined.err, prompt);
    EXPECT_EQ(declined.status, 0);
    EXPECT_TRUE(EntryTypeOf(*root, "/notes.txt").has_value());

    // End of input means no, and GNU prints nothing more: the prompt is the
    // last thing on the line.
    const auto ended = RunCaptured("rm", {"-i", "/notes.txt"});
    EXPECT_EQ(ended.out, kNone);
    EXPECT_EQ(ended.err, prompt);
    EXPECT_EQ(ended.status, 0);
    EXPECT_TRUE(EntryTypeOf(*root, "/notes.txt").has_value());

    const auto accepted = RunCaptured("rm", {"-iv", "/notes.txt"}, "y\n");
    EXPECT_EQ(accepted.out, "removed '/notes.txt'\n");
    EXPECT_EQ(accepted.err, prompt);
    EXPECT_EQ(accepted.status, 0);
    EXPECT_FALSE(EntryTypeOf(*root, "/notes.txt").has_value());
}

TEST_F(BuiltinCommandsTest, RmInteractiveEmptyFile) {
    WriteFile("/empty", "");
    const auto run = RunCaptured("rm", {"-i", "/empty"}, "n\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "rm: remove regular empty file '/empty'? ");
    EXPECT_EQ(run.status, 0);
    EXPECT_TRUE(EntryTypeOf(*root, "/empty").has_value());
}

TEST_F(BuiltinCommandsTest, RmInteractiveRecursiveTranscript) {
    ASSERT_EQ(root->CreateDirectory("/d", kDirMode), 0);
    ASSERT_EQ(root->CreateDirectory("/d/e", kDirMode), 0);
    WriteFile("/d/e/f", "content");

    // Every prompt declined where it matters: the file kept, the two
    // directories kept with it.
    const auto kept = RunCaptured("rm", {"-ri", "/d"}, "y\ny\nn\n");
    EXPECT_EQ(kept.out, kNone);
    EXPECT_EQ(kept.err,
        "rm: descend into directory '/d'? "
        "rm: descend into directory '/d/e'? "
        "rm: remove regular file '/d/e/f'? "
        "rm: remove directory '/d/e'? "
        "rm: remove directory '/d'? ");
    EXPECT_EQ(kept.status, 0);
    EXPECT_TRUE(EntryTypeOf(*root, "/d/e/f").has_value());
    EXPECT_EQ(EntryTypeOf(*root, "/d"), DirectoryEntryType::Dir);

    // The file declined, its directory accepted but not empty: it fails, and
    // its parent is skipped without a question.
    const auto failed = RunCaptured("rm", {"-ri", "/d"}, "y\ny\nn\ny\ny\n");
    EXPECT_EQ(failed.out, kNone);
    EXPECT_EQ(failed.err,
        "rm: descend into directory '/d'? "
        "rm: descend into directory '/d/e'? "
        "rm: remove regular file '/d/e/f'? "
        "rm: remove directory '/d/e'? "
        "rm: cannot remove '/d/e': Directory not empty\n");
    EXPECT_EQ(failed.status, 1);
    EXPECT_EQ(EntryTypeOf(*root, "/d/e/f"), DirectoryEntryType::File);
    EXPECT_EQ(EntryTypeOf(*root, "/d"), DirectoryEntryType::Dir);
}

TEST_F(BuiltinCommandsTest, RmPromptOnce) {
    WriteFile("/a", "");
    WriteFile("/b", "");
    WriteFile("/c", "");
    WriteFile("/dd", "");
    const auto many = RunCaptured("rm", {"-I", "/a", "/b", "/c", "/dd"}, "n\n");
    EXPECT_EQ(many.out, kNone);
    EXPECT_EQ(many.err, "rm: remove 4 arguments? ");
    EXPECT_EQ(many.status, 0);
    for (const auto& name : {"/a", "/b", "/c", "/dd"}) {
        EXPECT_TRUE(EntryTypeOf(*root, name).has_value()) << name;
    }

    // Three are not enough to ask.
    const auto few = RunCaptured("rm", {"-I", "/a", "/b", "/c"});
    EXPECT_EQ(few.out, kNone);
    EXPECT_EQ(few.err, kNone);
    EXPECT_EQ(few.status, 0);
    for (const auto& name : {"/a", "/b", "/c"}) {
        EXPECT_FALSE(EntryTypeOf(*root, name).has_value()) << name;
    }

    // Recursive removals always ask, one operand or more.
    const auto recursive = RunCaptured("rm", {"-rI", "/docs"}, "n\n");
    EXPECT_EQ(recursive.out, kNone);
    EXPECT_EQ(recursive.err, "rm: remove 1 argument recursively? ");
    EXPECT_EQ(recursive.status, 0);
    EXPECT_EQ(EntryTypeOf(*root, "/docs"), DirectoryEntryType::Dir);
}

TEST_F(BuiltinCommandsTest, RmLastOfForceAndInteractiveWins) {
    const auto prompting = RunCaptured("rm", {"-fi", "/notes.txt"}, "n\n");
    EXPECT_EQ(prompting.out, kNone);
    EXPECT_EQ(prompting.err, "rm: remove regular file '/notes.txt'? ");
    EXPECT_EQ(prompting.status, 0);
    EXPECT_TRUE(EntryTypeOf(*root, "/notes.txt").has_value());

    const auto silent = RunCaptured("rm", {"-if", "/notes.txt"}, "n\n");
    EXPECT_EQ(silent.out, kNone);
    EXPECT_EQ(silent.err, kNone);
    EXPECT_EQ(silent.status, 0);
    EXPECT_FALSE(EntryTypeOf(*root, "/notes.txt").has_value());
}

TEST_F(BuiltinCommandsTest, RmInteractiveBadWhen) {
    const auto run = RunCaptured("rm", {"--interactive=bogus", "/notes.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err,
        "rm: invalid argument 'bogus' for '--interactive'\n"
        "Valid arguments are:\n"
        "  - 'never', 'no', 'none'\n"
        "  - 'once'\n"
        "  - 'always', 'yes'\n"
        "Try 'rm --help' for more information.\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_TRUE(EntryTypeOf(*root, "/notes.txt").has_value());
}

TEST_F(BuiltinCommandsTest, RmRefusesDotAndDotDot) {
    const auto run = RunCaptured("rm", {"-r", "/docs/sub/.."});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "rm: refusing to remove '.' or '..' directory: skipping '/docs/sub/..'\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_EQ(EntryTypeOf(*root, "/docs"), DirectoryEntryType::Dir);
    EXPECT_EQ(EntryTypeOf(*root, "/docs/sub/b.md"), DirectoryEntryType::File);
}

TEST_F(BuiltinCommandsTest, RmPreservesRoot) {
    const auto run = RunCaptured("rm", {"-r", "/"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err,
        "rm: it is dangerous to operate recursively on '/'\n"
        "rm: use --no-preserve-root to override this failsafe\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_EQ(EntryTypeOf(*root, "/docs"), DirectoryEntryType::Dir);
    EXPECT_EQ(EntryTypeOf(*root, "/notes.txt"), DirectoryEntryType::File);
}

TEST_F(BuiltinCommandsTest, RmTrailingSlashOnAFile) {
    const auto run = RunCaptured("rm", {"/notes.txt/"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "rm: cannot remove '/notes.txt/': Not a directory\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_TRUE(EntryTypeOf(*root, "/notes.txt").has_value());
}

TEST_F(BuiltinCommandsTest, RmRefusesABuiltin) {
    const auto run = RunCaptured("rm", {"/bin/ls"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "rm: cannot remove '/bin/ls': Permission denied\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_EQ(root->IsBuiltinCommand("/bin/ls").value_or(""), "ls");
}

TEST_F(BuiltinCommandsTest, RmRecursiveKeepsADirectoryHoldingBuiltins) {
    const auto run = RunCaptured("rm", {"-r", "/bin"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.status, 1);
    EXPECT_TRUE(Contains(SplitLines(run.err), "rm: cannot remove '/bin/cat': Permission denied"));
    // The directory itself is skipped silently: a message of its own would
    // name no fault of its own.
    EXPECT_EQ(run.err.find("'/bin':"), std::string::npos) << run.err;
    EXPECT_EQ(EntryTypeOf(*root, "/bin"), DirectoryEntryType::Dir);
    EXPECT_EQ(root->IsBuiltinCommand("/bin/cat").value_or(""), "cat");
}

TEST_F(BuiltinCommandsTest, RmBusyMountPoint) {
    ASSERT_EQ(root->CreateDirectory("/mnt", kDirMode), 0);
    root->Mount("/mnt", factory->CreateServicesCreator()->CreateFileSystemService()->CreateEmptyInMemFileSystem());
    const auto run = RunCaptured("rm", {"-r", "/mnt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "rm: cannot remove '/mnt': Device or resource busy\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_EQ(EntryTypeOf(*root, "/mnt"), DirectoryEntryType::Dir);
}

// --- rmdir ---

TEST_F(BuiltinCommandsTest, RmdirRemovesEmptyDirectories) {
    ASSERT_EQ(root->CreateDirectory("/e", kDirMode), 0);
    const auto run = RunCaptured("rmdir", {"/e"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_FALSE(EntryTypeOf(*root, "/e").has_value());
}

TEST_F(BuiltinCommandsTest, RmdirMessages) {
    const auto missing = RunCaptured("rmdir", {});
    EXPECT_EQ(missing.out, kNone);
    EXPECT_EQ(missing.err, "rmdir: missing operand\nTry 'rmdir --help' for more information.\n");
    EXPECT_EQ(missing.status, 1);

    const auto file = RunCaptured("rmdir", {"/notes.txt"});
    EXPECT_EQ(file.out, kNone);
    EXPECT_EQ(file.err, "rmdir: failed to remove '/notes.txt': Not a directory\n");
    EXPECT_EQ(file.status, 1);

    const auto nothing = RunCaptured("rmdir", {"/nothing"});
    EXPECT_EQ(nothing.out, kNone);
    EXPECT_EQ(nothing.err, "rmdir: failed to remove '/nothing': No such file or directory\n");
    EXPECT_EQ(nothing.status, 1);

    const auto full = RunCaptured("rmdir", {"/docs"});
    EXPECT_EQ(full.out, kNone);
    EXPECT_EQ(full.err, "rmdir: failed to remove '/docs': Directory not empty\n");
    EXPECT_EQ(full.status, 1);

    const auto ignored = RunCaptured("rmdir", {"--ignore-fail-on-non-empty", "/docs"});
    EXPECT_EQ(ignored.out, kNone);
    EXPECT_EQ(ignored.err, kNone);
    EXPECT_EQ(ignored.status, 0);
    EXPECT_EQ(EntryTypeOf(*root, "/docs"), DirectoryEntryType::Dir);
}

TEST_F(BuiltinCommandsTest, RmdirParentsVerbose) {
    ASSERT_EQ(root->CreateDirectory("/a", kDirMode), 0);
    ASSERT_EQ(root->CreateDirectory("/a/b", kDirMode), 0);
    ASSERT_EQ(root->CreateDirectory("/a/b/c", kDirMode), 0);
    const auto run = RunCaptured("rmdir", {"-pv", "a/b/c"});
    EXPECT_EQ(run.out,
        "rmdir: removing directory, 'a/b/c'\n"
        "rmdir: removing directory, 'a/b'\n"
        "rmdir: removing directory, 'a'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_FALSE(EntryTypeOf(*root, "/a").has_value());
}

TEST_F(BuiltinCommandsTest, RmdirParentFailure) {
    ASSERT_EQ(root->CreateDirectory("/q", kDirMode), 0);
    ASSERT_EQ(root->CreateDirectory("/q/r", kDirMode), 0);
    WriteFile("/q/keep", "still here");
    const auto run = RunCaptured("rmdir", {"-pv", "q/r"});
    EXPECT_EQ(run.out,
        "rmdir: removing directory, 'q/r'\n"
        "rmdir: removing directory, 'q'\n");
    EXPECT_EQ(run.err, "rmdir: failed to remove directory 'q': Directory not empty\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_FALSE(EntryTypeOf(*root, "/q/r").has_value());

    ASSERT_EQ(root->CreateDirectory("/q/r", kDirMode), 0);
    const auto ignored = RunCaptured("rmdir", {"--ignore-fail-on-non-empty", "-p", "q/r"});
    EXPECT_EQ(ignored.out, kNone);
    EXPECT_EQ(ignored.err, kNone);
    EXPECT_EQ(ignored.status, 0);
    EXPECT_FALSE(EntryTypeOf(*root, "/q/r").has_value());
    EXPECT_EQ(EntryTypeOf(*root, "/q/keep"), DirectoryEntryType::File);
}

TEST_F(BuiltinCommandsTest, RmdirBusyMountPoint) {
    ASSERT_EQ(root->CreateDirectory("/mnt", kDirMode), 0);
    root->Mount("/mnt", factory->CreateServicesCreator()->CreateFileSystemService()->CreateEmptyInMemFileSystem());
    const auto run = RunCaptured("rmdir", {"/mnt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "rmdir: failed to remove '/mnt': Device or resource busy\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_EQ(EntryTypeOf(*root, "/mnt"), DirectoryEntryType::Dir);
}