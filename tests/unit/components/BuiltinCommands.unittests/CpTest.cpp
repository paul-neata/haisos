#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"

using namespace Haisos;

namespace {

const std::string kNone;
const std::string kTryCp = "Try 'cp --help' for more information.\n";
const char* const kNotesContent = "one\ntwo\n\n\n\tthree\n";

} // namespace

// --- cp ---

TEST_F(BuiltinCommandsTest, CpCopiesAFile) {
    const auto run = RunCaptured("cp", {"/notes.txt", "/n2"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/n2", content));
    EXPECT_EQ(content, kNotesContent);
}

TEST_F(BuiltinCommandsTest, CpVerboseIntoADirectory) {
    const auto run = RunCaptured("cp", {"-v", "/notes.txt", "/docs/a.md", "/docs/sub"});
    EXPECT_EQ(run.out,
        "'/notes.txt' -> '/docs/sub/notes.txt'\n"
        "'/docs/a.md' -> '/docs/sub/a.md'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, CpOperandErrors) {
    auto run = RunCaptured("cp", {});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "cp: missing file operand\n" + kTryCp);
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("cp", {"/notes.txt"});
    EXPECT_EQ(run.err, "cp: missing destination file operand after '/notes.txt'\n" + kTryCp);
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("cp", {"/notes.txt", "/.hidden", "/x"});
    EXPECT_EQ(run.err, "cp: target '/x': No such file or directory\n");
    EXPECT_EQ(run.status, 1);

    WriteFile("/x", "file");
    run = RunCaptured("cp", {"/notes.txt", "/.hidden", "/x"});
    EXPECT_EQ(run.err, "cp: target '/x': Not a directory\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("cp", {"-t", "/nope", "/notes.txt"});
    EXPECT_EQ(run.err, "cp: target directory '/nope': No such file or directory\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("cp", {"-t", "/x", "/notes.txt"});
    EXPECT_EQ(run.err, "cp: target directory '/x': Not a directory\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("cp", {"-t", "/docs", "-T", "/a", "/b"});
    EXPECT_EQ(run.err, "cp: cannot combine --target-directory (-t) and --no-target-directory (-T)\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("cp", {"-T", "/a", "/b", "/c"});
    EXPECT_EQ(run.err, "cp: extra operand '/c'\n" + kTryCp);
    EXPECT_EQ(run.status, 1);

    // -T with one operand names the missing destination, as GNU does.
    run = RunCaptured("cp", {"-T", "/notes.txt"});
    EXPECT_EQ(run.err, "cp: missing destination file operand after '/notes.txt'\n" + kTryCp);
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, CpMissingSource) {
    const auto run = RunCaptured("cp", {"/nothing", "/x"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "cp: cannot stat '/nothing': No such file or directory\n");
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, CpDirectoryWithoutR) {
    const auto run = RunCaptured("cp", {"/docs", "/d2"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "cp: -r not specified; omitting directory '/docs'\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_FALSE(EntryTypeOf(*root, "/d2").has_value());
}

TEST_F(BuiltinCommandsTest, CpRecursiveVerbose) {
    auto run = RunCaptured("cp", {"-rv", "/docs", "/d2"});
    EXPECT_EQ(run.out,
        "'/docs' -> '/d2'\n"
        "'/docs/a.md' -> '/d2/a.md'\n"
        "'/docs/sub' -> '/d2/sub'\n"
        "'/docs/sub/b.md' -> '/d2/sub/b.md'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // The second run copies into the directory the first made.
    run = RunCaptured("cp", {"-rv", "/docs", "/d2"});
    EXPECT_EQ(run.out,
        "'/docs' -> '/d2/docs'\n"
        "'/docs/a.md' -> '/d2/docs/a.md'\n"
        "'/docs/sub' -> '/d2/docs/sub'\n"
        "'/docs/sub/b.md' -> '/d2/docs/sub/b.md'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, CpTrailingSlashSource) {
    const auto run = RunCaptured("cp", {"-rv", "docs/", "u"});
    EXPECT_EQ(run.out,
        "'docs/' -> 'u'\n"
        "'docs/a.md' -> 'u/a.md'\n"
        "'docs/sub' -> 'u/sub'\n"
        "'docs/sub/b.md' -> 'u/sub/b.md'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, CpSameFile) {
    const auto run = RunCaptured("cp", {"notes.txt", "./"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "cp: 'notes.txt' and './notes.txt' are the same file\n");
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, CpIntoItself) {
    const auto run = RunCaptured("cp", {"-r", "/docs", "/docs/sub"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "cp: cannot copy a directory, '/docs', into itself, '/docs/sub/docs'\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_FALSE(EntryTypeOf(*root, "/docs/sub/docs").has_value());
}

TEST_F(BuiltinCommandsTest, CpTypeMismatch) {
    auto run = RunCaptured("cp", {"-r", "/docs", "/notes.txt"});
    EXPECT_EQ(run.err, "cp: cannot overwrite non-directory '/notes.txt' with directory '/docs'\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("cp", {"-T", "/notes.txt", "/docs"});
    EXPECT_EQ(run.err, "cp: cannot overwrite directory '/docs' with non-directory\n");
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, CpCreateFailures) {
    auto run = RunCaptured("cp", {"/notes.txt", "/missing/x"});
    EXPECT_EQ(run.err, "cp: cannot create regular file '/missing/x': No such file or directory\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("cp", {"/notes.txt", "/nodir/"});
    EXPECT_EQ(run.err, "cp: cannot create regular file '/nodir/': Not a directory\n");
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, CpPreservesTimes) {
    const FileDateTime fixed{1704164645, 0};
    ASSERT_EQ(root->SetTimes("/notes.txt", fixed, fixed), 0);

    auto run = RunCaptured("cp", {"-p", "/notes.txt", "/p"});
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    FileStatus status;
    ASSERT_EQ(root->Stat("/p", status), 0);
    EXPECT_EQ(status.accessTime, fixed);
    EXPECT_EQ(status.modificationTime, fixed);

    ASSERT_EQ(root->SetTimes("/docs/a.md", fixed, fixed), 0);
    ASSERT_EQ(root->SetTimes("/docs", fixed, fixed), 0);
    run = RunCaptured("cp", {"-a", "/docs", "/a2"});
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    ASSERT_EQ(root->Stat("/a2", status), 0);
    EXPECT_EQ(status.modificationTime, fixed);
    ASSERT_EQ(root->Stat("/a2/a.md", status), 0);
    EXPECT_EQ(status.accessTime, fixed);
    EXPECT_EQ(status.modificationTime, fixed);

    // A plain copy takes the time of copying, not the source's.
    run = RunCaptured("cp", {"/notes.txt", "/plain"});
    EXPECT_EQ(run.status, 0);
    ASSERT_EQ(root->Stat("/plain", status), 0);
    EXPECT_NE(status.modificationTime, fixed);
}

TEST_F(BuiltinCommandsTest, CpNoClobberWarns) {
    const auto run = RunCaptured("cp", {"-n", "/.hidden", "/notes.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "cp: warning: behavior of -n is non-portable and may change in future; use --update=none instead\n");
    EXPECT_EQ(run.status, 0);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/notes.txt", content));
    EXPECT_EQ(content, kNotesContent);

    const auto updated = RunCaptured("cp", {"--update=none", "-v", "/.hidden", "/notes.txt"});
    EXPECT_EQ(updated.out, kNone);
    EXPECT_EQ(updated.err, kNone);
    EXPECT_EQ(updated.status, 0);
}

TEST_F(BuiltinCommandsTest, CpInteractive) {
    const std::string prompt = "cp: overwrite '/notes.txt'? ";
    auto run = RunCaptured("cp", {"-i", "/.hidden", "/notes.txt"}, "n\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, prompt);
    EXPECT_EQ(run.status, 1);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/notes.txt", content));
    EXPECT_EQ(content, kNotesContent);

    run = RunCaptured("cp", {"-iv", "/.hidden", "/notes.txt"}, "y\n");
    EXPECT_EQ(run.out, "'/.hidden' -> '/notes.txt'\n");
    EXPECT_EQ(run.err, prompt);
    EXPECT_EQ(run.status, 0);
    ASSERT_TRUE(ReadWholeFile(*root, "/notes.txt", content));
    EXPECT_EQ(content, "h");

    // A destination that does not exist is no question at all.
    run = RunCaptured("cp", {"-i", "/.hidden", "/brandnew"}, "n\n");
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, CpUpdateOlder) {
    const FileDateTime oldTime{1704164645, 0};       // 2024-01-02
    const FileDateTime newerTime{1735900000, 0};     // 2025-01-03
    const FileDateTime olderTime{1600000000, 0};     // 2020-09-13
    ASSERT_EQ(root->SetTimes("/notes.txt", oldTime, oldTime), 0);
    WriteFile("/dest", "old content");
    ASSERT_EQ(root->SetTimes("/dest", newerTime, newerTime), 0);

    auto run = RunCaptured("cp", {"-uv", "/notes.txt", "/dest"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/dest", content));
    EXPECT_EQ(content, "old content");

    ASSERT_EQ(root->SetTimes("/dest", olderTime, olderTime), 0);
    run = RunCaptured("cp", {"-uv", "/notes.txt", "/dest"});
    EXPECT_EQ(run.out, "'/notes.txt' -> '/dest'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    ASSERT_TRUE(ReadWholeFile(*root, "/dest", content));
    EXPECT_EQ(content, kNotesContent);
}

TEST_F(BuiltinCommandsTest, CpBackups) {
    auto run = RunCaptured("cp", {"-bv", "/.hidden", "/notes.txt"});
    EXPECT_EQ(run.out, "'/.hidden' -> '/notes.txt' (backup: '/notes.txt~')\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/notes.txt~", content));
    EXPECT_EQ(content, kNotesContent);

    run = RunCaptured("cp", {"-v", "--backup=numbered", "/.hidden", "/notes.txt"});
    EXPECT_EQ(run.out, "'/.hidden' -> '/notes.txt' (backup: '/notes.txt.~1~')\n");
    EXPECT_EQ(run.status, 0);
    run = RunCaptured("cp", {"-v", "--backup=numbered", "/.hidden", "/notes.txt"});
    EXPECT_EQ(run.out, "'/.hidden' -> '/notes.txt' (backup: '/notes.txt.~2~')\n");
    EXPECT_EQ(run.status, 0);
    EXPECT_EQ(EntryTypeOf(*root, "/notes.txt.~1~"), DirectoryEntryType::File);
    EXPECT_EQ(EntryTypeOf(*root, "/notes.txt.~2~"), DirectoryEntryType::File);

    // A suffix alone backs up too, as GNU 9.4 does.
    WriteFile("/x", "before");
    run = RunCaptured("cp", {"-v", "-S", ".bak", "/notes.txt", "/x"});
    EXPECT_EQ(run.out, "'/notes.txt' -> '/x' (backup: '/x.bak')\n");
    EXPECT_EQ(run.status, 0);
    ASSERT_TRUE(ReadWholeFile(*root, "/x.bak", content));
    EXPECT_EQ(content, "before");

    run = RunCaptured("cp", {"--backup=bogus", "/notes.txt", "/x"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err,
        "cp: invalid argument 'bogus' for 'backup type'\n"
        "Valid arguments are:\n"
        "  - 'none', 'off'\n"
        "  - 'simple', 'never'\n"
        "  - 'existing', 'nil'\n"
        "  - 'numbered', 't'\n" + kTryCp);
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, CpRemoveDestination) {
    const auto run = RunCaptured("cp", {"-v", "--remove-destination", "/.hidden", "/notes.txt"});
    EXPECT_EQ(run.out,
        "removed '/notes.txt'\n"
        "'/.hidden' -> '/notes.txt'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, CpAttributesOnly) {
    const auto run = RunCaptured("cp", {"--attributes-only", "/notes.txt", "/ao"});
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    FileStatus status;
    ASSERT_EQ(root->Stat("/ao", status), 0);
    EXPECT_EQ(status.size, 0u);

    // An existing destination keeps its data, as GNU does.
    WriteFile("/ao2", "kept");
    const auto kept = RunCaptured("cp", {"--attributes-only", "/notes.txt", "/ao2"});
    EXPECT_EQ(kept.status, 0);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/ao2", content));
    EXPECT_EQ(content, "kept");
}

TEST_F(BuiltinCommandsTest, CpParentsVerbose) {
    ASSERT_EQ(root->CreateDirectory("/p", kDirMode), 0);
    const auto run = RunCaptured("cp", {"-v", "--parents", "docs/sub/b.md", "p"});
    EXPECT_EQ(run.out,
        "docs -> p/docs\n"
        "docs/sub -> p/docs/sub\n"
        "'docs/sub/b.md' -> 'p/docs/sub/b.md'\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_EQ(EntryTypeOf(*root, "/p/docs/sub/b.md"), DirectoryEntryType::File);

    // The same copy again: the directories are there, so only the file's line.
    const auto again = RunCaptured("cp", {"-v", "--parents", "docs/sub/b.md", "p"});
    EXPECT_EQ(again.out, "'docs/sub/b.md' -> 'p/docs/sub/b.md'\n");
    EXPECT_EQ(again.status, 0);

    const auto missing = RunCaptured("cp", {"--parents", "/notes.txt", "/b"});
    EXPECT_EQ(missing.err, "cp: with --parents, the destination must be a directory\n" + kTryCp);
    EXPECT_EQ(missing.status, 1);
}

TEST_F(BuiltinCommandsTest, CpLinksFail) {
    auto run = RunCaptured("cp", {"-l", "/notes.txt", "/lnk"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "cp: cannot create hard link '/lnk' to '/notes.txt': Operation not permitted\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_FALSE(EntryTypeOf(*root, "/lnk").has_value());

    run = RunCaptured("cp", {"-s", "/notes.txt", "/lnk"});
    EXPECT_EQ(run.err, "cp: cannot create symbolic link '/lnk' to '/notes.txt': Operation not permitted\n");
    EXPECT_EQ(run.status, 1);
    EXPECT_FALSE(EntryTypeOf(*root, "/lnk").has_value());
}

TEST_F(BuiltinCommandsTest, CpPreserveBadAttribute) {
    const auto run = RunCaptured("cp", {"--preserve=bogus", "/notes.txt", "/x"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err,
        "cp: invalid argument 'bogus' for '--preserve'\n"
        "Valid arguments are:\n"
        "  - 'mode'\n"
        "  - 'timestamps'\n"
        "  - 'ownership'\n"
        "  - 'links'\n"
        "  - 'context'\n"
        "  - 'xattr'\n"
        "  - 'all'\n" + kTryCp);
    EXPECT_EQ(run.status, 1);

    // The accepted words that keep nothing: no error, nothing not-treated.
    const auto links = RunCaptured("cp", {"--preserve=links", "/notes.txt", "/x"});
    EXPECT_EQ(links.err, kNone);
    EXPECT_EQ(links.status, 0);

    // context and xattr are accepted and reported as not treated.
    const auto context = RunCaptured("cp", {"--preserve=context", "/notes.txt", "/x"});
    EXPECT_EQ(context.err, "Parameter --preserve=context is not treated by HaisosOS cp v. 1.0.0\n");
    EXPECT_EQ(context.status, 0);
}

TEST_F(BuiltinCommandsTest, CpCopiesABuiltinsNote) {
    const auto run = RunCaptured("cp", {"/bin/ls", "/ls.txt"});
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    std::string copied;
    ASSERT_TRUE(ReadWholeFile(*root, "/ls.txt", copied));
    std::string note;
    ASSERT_TRUE(ReadWholeFile(*root, "/bin/ls", note));
    EXPECT_EQ(copied, note);
}

TEST_F(BuiltinCommandsTest, CpAcrossAMount) {
    auto mnt = factory->CreateServicesCreator()->CreateFileSystemService()->CreateEmptyInMemFileSystem();
    root->Mount("/mnt", mnt);
    const auto run = RunCaptured("cp", {"-r", "/docs", "/mnt/d"});
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_EQ(EntryTypeOf(*root, "/mnt/d"), DirectoryEntryType::Dir);
    EXPECT_EQ(EntryTypeOf(*root, "/mnt/d/a.md"), DirectoryEntryType::File);
    EXPECT_EQ(EntryTypeOf(*root, "/mnt/d/sub"), DirectoryEntryType::Dir);
    EXPECT_EQ(EntryTypeOf(*root, "/mnt/d/sub/b.md"), DirectoryEntryType::File);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/mnt/d/sub/b.md", content));
    EXPECT_EQ(content, "bravo!");
}

TEST_F(BuiltinCommandsTest, CpUpdateWordAndDereferenceNoOps) {
    // --update's words, and the dereference options, which change nothing.
    auto run = RunCaptured("cp", {"--update=bogus", "/notes.txt", "/x"});
    EXPECT_EQ(run.err,
        "cp: invalid argument 'bogus' for '--update'\n"
        "Valid arguments are:\n"
        "  - 'all'\n"
        "  - 'none'\n"
        "  - 'older'\n" + kTryCp);
    EXPECT_EQ(run.status, 1);

    WriteFile("/x", "here");
    run = RunCaptured("cp", {"-dHLp", "/notes.txt", "/x"});
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // -i after -n prompts again, as GNU: the last of -i/-n wins.
    const auto prompted = RunCaptured("cp", {"-n", "-i", "/.hidden", "/x"}, "y\n");
    EXPECT_EQ(prompted.err, "cp: warning: behavior of -n is non-portable and may change in future; use --update=none instead\ncp: overwrite '/x'? ");
    EXPECT_EQ(prompted.status, 0);
}
TEST_F(BuiltinCommandsTest, CpBackupModeAtTheEnd) {
    auto environment = factory->CreateEnvironment();
    environment->SetVariable("VERSION_CONTROL", "bogus");

    // -b takes $VERSION_CONTROL's word, and a bad one is reported under that
    // name -- after the option parsing, not where -b was met.
    const std::string badVersionControl =
        "cp: invalid argument 'bogus' for '$VERSION_CONTROL'\n"
        "Valid arguments are:\n"
        "  - 'none', 'off'\n"
        "  - 'simple', 'never'\n"
        "  - 'existing', 'nil'\n"
        "  - 'numbered', 't'\n" + kTryCp;
    auto run = RunCaptured("cp", {"-b", "/notes.txt", "/x"}, std::nullopt, "/", environment);
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, badVersionControl);
    EXPECT_EQ(run.status, 1);

    // -S alone turns backups on too, so it is checked then as well.
    run = RunCaptured("cp", {"-S", ".bak", "/notes.txt", "/x"}, std::nullopt, "/", environment);
    EXPECT_EQ(run.err, badVersionControl);
    EXPECT_EQ(run.status, 1);

    // --backup=WORD is its own word: $VERSION_CONTROL is not looked at.
    environment->SetVariable("VERSION_CONTROL", "numbered");
    WriteFile("/x", "here");
    run = RunCaptured("cp", {"-v", "--backup=simple", "/notes.txt", "/x"},
                      std::nullopt, "/", environment);
    EXPECT_EQ(run.out, "'/notes.txt' -> '/x' (backup: '/x~')\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, CpNoClobberBeatsLaterUpdate) {
    // -n wins over a later --update=WORD: an older destination is still not
    // replaced.
    const FileDateTime oldTime{1704164645, 0};
    WriteFile("/dest", "old");
    ASSERT_EQ(root->SetTimes("/dest", oldTime, oldTime), 0);

    const auto run = RunCaptured("cp", {"-n", "--update=older", "/notes.txt", "/dest"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "cp: warning: behavior of -n is non-portable and may change in future; use --update=none instead\n");
    EXPECT_EQ(run.status, 0);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/dest", content));
    EXPECT_EQ(content, "old");
}
