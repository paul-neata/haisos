#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"

using namespace Haisos;

namespace {

const std::string kNone;
const std::string kTryMv = "Try 'mv --help' for more information.\n";
const char* const kNotesContent = "one\ntwo\n\n\n\tthree\n";

// A bad $VERSION_CONTROL, as -b (and -S, which turns backups on too) report
// it at the end of the option parsing.
const std::string kBadVersionControl =
    "mv: invalid argument 'bogus' for '$VERSION_CONTROL'\n"
    "Valid arguments are:\n"
    "  - 'none', 'off'\n"
    "  - 'simple', 'never'\n"
    "  - 'existing', 'nil'\n"
    "  - 'numbered', 't'\n" + kTryMv;

} // namespace

// --- mv ---

TEST_F(BuiltinCommandsTest, MvRenamesVerbose) {
    const auto run = RunCaptured("mv", {"-v", "/notes.txt", "/n"});
    EXPECT_EQ(run.out, "renamed '/notes.txt' -> '/n'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_FALSE(EntryTypeOf(*root, "/notes.txt").has_value());
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/n", content));
    EXPECT_EQ(content, kNotesContent);
}

TEST_F(BuiltinCommandsTest, MvIntoADirectory) {
    const auto run = RunCaptured("mv", {"-v", "/notes.txt", "/.hidden", "/docs/sub"});
    EXPECT_EQ(run.out,
        "renamed '/notes.txt' -> '/docs/sub/notes.txt'\n"
        "renamed '/.hidden' -> '/docs/sub/.hidden'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, MvOperandErrors) {
    auto run = RunCaptured("mv", {});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "mv: missing file operand\n" + kTryMv);
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("mv", {"/notes.txt"});
    EXPECT_EQ(run.err, "mv: missing destination file operand after '/notes.txt'\n" + kTryMv);
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("mv", {"/nothing", "/x"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "mv: cannot stat '/nothing': No such file or directory\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("mv", {"-t", "/nodir", "/notes.txt"});
    EXPECT_EQ(run.err, "mv: target directory '/nodir': No such file or directory\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("mv", {"/docs/a.md", "/docs/sub/b.md", "/notes.txt"});
    EXPECT_EQ(run.err, "mv: target '/notes.txt': Not a directory\n");
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, MvRefusals) {
    auto run = RunCaptured("mv", {"/docs", "/docs/sub"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "mv: cannot move '/docs' to a subdirectory of itself, '/docs/sub/docs'\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("mv", {"/docs", "/notes.txt"});
    EXPECT_EQ(run.err, "mv: cannot overwrite non-directory '/notes.txt' with directory '/docs'\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("mv", {"-T", "/notes.txt", "/docs"});
    EXPECT_EQ(run.err, "mv: cannot overwrite directory '/docs' with non-directory\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("mv", {"/notes.txt", "/notes.txt"});
    EXPECT_EQ(run.err, "mv: '/notes.txt' and '/notes.txt' are the same file\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("mv", {".", "x"}, std::nullopt, "/docs");
    EXPECT_EQ(run.err, "mv: cannot move '.' to 'x': Device or resource busy\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("mv", {"/bin/ls", "/ls"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "mv: cannot move '/bin/ls' to '/ls': Permission denied\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_FALSE(EntryTypeOf(*root, "/ls").has_value());
}

TEST_F(BuiltinCommandsTest, MvNoClobberInteractiveUpdateBackup) {
    // A source older than the destination, so -u would skip it.
    const FileDateTime oldTime{1704164645, 0};
    ASSERT_EQ(root->SetTimes("/notes.txt", oldTime, oldTime), 0);
    std::string content;

    auto run = RunCaptured("mv", {"-n", "/notes.txt", "/.hidden"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "mv: not replacing '/.hidden'\n");
    EXPECT_EQ(run.status, 1);
    ASSERT_TRUE(ReadWholeFile(*root, "/.hidden", content));
    EXPECT_EQ(content, "h");
    EXPECT_TRUE(EntryTypeOf(*root, "/notes.txt").has_value());

    run = RunCaptured("mv", {"-i", "/notes.txt", "/.hidden"}, "n\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "mv: overwrite '/.hidden'? ");
    EXPECT_EQ(run.status, 1);
    ASSERT_TRUE(ReadWholeFile(*root, "/.hidden", content));
    EXPECT_EQ(content, "h");

    // -u: the destination is newer, so the source is left alone silently.
    run = RunCaptured("mv", {"-u", "/notes.txt", "/.hidden"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    ASSERT_TRUE(ReadWholeFile(*root, "/.hidden", content));
    EXPECT_EQ(content, "h");
    EXPECT_TRUE(EntryTypeOf(*root, "/notes.txt").has_value());

    run = RunCaptured("mv", {"-bv", "/notes.txt", "/.hidden"});
    EXPECT_EQ(run.out, "renamed '/notes.txt' -> '/.hidden' (backup: '/.hidden~')\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    ASSERT_TRUE(ReadWholeFile(*root, "/.hidden", content));
    EXPECT_EQ(content, kNotesContent);
    ASSERT_TRUE(ReadWholeFile(*root, "/.hidden~", content));
    EXPECT_EQ(content, "h");
    EXPECT_FALSE(EntryTypeOf(*root, "/notes.txt").has_value());
}

TEST_F(BuiltinCommandsTest, MvMissingParent) {
    const auto run = RunCaptured("mv", {"/notes.txt", "/nodir/x"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "mv: cannot move '/notes.txt' to '/nodir/x': No such file or directory\n");
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, MvAcrossAMountCopiesThenRemoves) {
    auto mnt = factory->CreateServicesCreator()->CreateFileSystemService()->CreateEmptyInMemFileSystem();
    root->Mount("/mnt", mnt);
    const FileDateTime fixed{1704164645, 0};
    ASSERT_EQ(root->SetTimes("/docs/a.md", fixed, fixed), 0);

    auto run = RunCaptured("mv", {"-v", "/docs", "/mnt/d"});
    EXPECT_EQ(run.out,
        "created directory '/mnt/d'\n"
        "copied '/docs/a.md' -> '/mnt/d/a.md'\n"
        "created directory '/mnt/d/sub'\n"
        "copied '/docs/sub/b.md' -> '/mnt/d/sub/b.md'\n"
        "removed '/docs/a.md'\n"
        "removed '/docs/sub/b.md'\n"
        "removed directory '/docs/sub'\n"
        "removed directory '/docs'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_FALSE(EntryTypeOf(*root, "/docs").has_value());
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/mnt/d/a.md", content));
    EXPECT_EQ(content, "alpha");
    FileStatus status;
    ASSERT_EQ(root->Stat("/mnt/d/a.md", status), 0);
    EXPECT_EQ(status.modificationTime, fixed);

    // A file: one copy line, then its removal.
    run = RunCaptured("mv", {"-v", "/notes.txt", "/mnt/n"});
    EXPECT_EQ(run.out,
        "copied '/notes.txt' -> '/mnt/n'\n"
        "removed '/notes.txt'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_FALSE(EntryTypeOf(*root, "/notes.txt").has_value());
    ASSERT_TRUE(ReadWholeFile(*root, "/mnt/n", content));
    EXPECT_EQ(content, kNotesContent);
}

TEST_F(BuiltinCommandsTest, MvNoCopyAcrossAMount) {
    auto mnt = factory->CreateServicesCreator()->CreateFileSystemService()->CreateEmptyInMemFileSystem();
    root->Mount("/mnt", mnt);
    const auto run = RunCaptured("mv", {"--no-copy", "/notes.txt", "/mnt/n"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "mv: cannot move '/notes.txt' to '/mnt/n': Invalid cross-device link\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_FALSE(EntryTypeOf(*root, "/mnt/n").has_value());
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/notes.txt", content));
    EXPECT_EQ(content, kNotesContent);
}

// --- mv: the review fixes shared with cp ---

TEST_F(BuiltinCommandsTest, MvBackupModeAtTheEnd) {
    auto environment = factory->CreateEnvironment();
    environment->SetVariable("VERSION_CONTROL", "bogus");

    // -b takes $VERSION_CONTROL's word, and a bad one is reported under that
    // name -- after the option parsing, not where -b was met.
    auto run = RunCaptured("mv", {"-b", "/notes.txt", "/x"}, std::nullopt, "/", environment);
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kBadVersionControl);
    EXPECT_EQ(run.status, 1);

    // -S alone turns backups on too, so it is checked then as well.
    run = RunCaptured("mv", {"-S", ".bak", "/notes.txt", "/x"}, std::nullopt, "/", environment);
    EXPECT_EQ(run.err, kBadVersionControl);
    EXPECT_EQ(run.status, 1);

    // --backup=WORD is its own word: $VERSION_CONTROL is not looked at.
    environment->SetVariable("VERSION_CONTROL", "numbered");
    WriteFile("/x", "here");
    run = RunCaptured("mv", {"-v", "--backup=simple", "/notes.txt", "/x"},
                      std::nullopt, "/", environment);
    EXPECT_EQ(run.out, "renamed '/notes.txt' -> '/x' (backup: '/x~')\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, MvNoClobberBeatsLaterUpdate) {
    // -n wins over a later --update=WORD: an older destination is still not
    // replaced.
    const FileDateTime oldTime{1704164645, 0};
    WriteFile("/dest", "old");
    ASSERT_EQ(root->SetTimes("/dest", oldTime, oldTime), 0);

    const auto run = RunCaptured("mv", {"-n", "--update=older", "/notes.txt", "/dest"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "mv: not replacing '/dest'\n");
    EXPECT_EQ(run.status, 1);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/dest", content));
    EXPECT_EQ(content, "old");
    EXPECT_TRUE(EntryTypeOf(*root, "/notes.txt").has_value());
}