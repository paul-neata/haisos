#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "BuiltinDate.h"

using namespace Haisos;

namespace {

const std::string kNone;
const std::string kTryTouch = "Try 'touch --help' for more information.\n";
const char* const kNotesContent = "one\ntwo\n\n\n\tthree\n";

} // namespace

// --- touch ---

TEST_F(BuiltinCommandsTest, TouchCreatesAnEmptyFile) {
    auto run = RunCaptured("touch", {"/new"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_EQ(EntryTypeOf(*root, "/new"), DirectoryEntryType::File);
    FileStatus status;
    ASSERT_EQ(root->Stat("/new", status), 0);
    EXPECT_EQ(status.size, 0u);

    // -h is the same as without it (no links), -f is accepted and ignored.
    run = RunCaptured("touch", {"-h", "/new2"});
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_EQ(EntryTypeOf(*root, "/new2"), DirectoryEntryType::File);
    run = RunCaptured("touch", {"-f", "/new3"});
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_EQ(EntryTypeOf(*root, "/new3"), DirectoryEntryType::File);
}

TEST_F(BuiltinCommandsTest, TouchNoCreate) {
    const auto run = RunCaptured("touch", {"-c", "/nothing"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    EXPECT_FALSE(EntryTypeOf(*root, "/nothing").has_value());
}

TEST_F(BuiltinCommandsTest, TouchMissingDirectory) {
    const auto run = RunCaptured("touch", {"/nodir/x"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "touch: cannot touch '/nodir/x': No such file or directory\n");
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, TouchMissingOperand) {
    const auto run = RunCaptured("touch", {});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "touch: missing file operand\n" + kTryTouch);
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, TouchDateUtc) {
    const auto run = RunCaptured("touch", {"-d", "2024-01-02T03:04:05Z", "/notes.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    FileStatus status;
    ASSERT_EQ(root->Stat("/notes.txt", status), 0);
    EXPECT_EQ(status.accessTime, (FileDateTime{1704164645, 0}));
    EXPECT_EQ(status.modificationTime, (FileDateTime{1704164645, 0}));
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/notes.txt", content));
    EXPECT_EQ(content, kNotesContent);
}

TEST_F(BuiltinCommandsTest, TouchAccessOrModificationOnly) {
    const FileDateTime fixed{1700000000, 0};
    ASSERT_EQ(root->SetTimes("/notes.txt", fixed, fixed), 0);
    FileStatus status;

    // -a changes the access time only.
    auto run = RunCaptured("touch", {"-a", "-d", "@100", "/notes.txt"});
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    ASSERT_EQ(root->Stat("/notes.txt", status), 0);
    EXPECT_EQ(status.accessTime, (FileDateTime{100, 0}));
    EXPECT_EQ(status.modificationTime, fixed);

    // -m changes the modification time only.
    run = RunCaptured("touch", {"-m", "-d", "@200", "/notes.txt"});
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    ASSERT_EQ(root->Stat("/notes.txt", status), 0);
    EXPECT_EQ(status.accessTime, (FileDateTime{100, 0}));
    EXPECT_EQ(status.modificationTime, (FileDateTime{200, 0}));

    // --time=atime is -a's word form.
    run = RunCaptured("touch", {"--time=atime", "-d", "@300", "/notes.txt"});
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    ASSERT_EQ(root->Stat("/notes.txt", status), 0);
    EXPECT_EQ(status.accessTime, (FileDateTime{300, 0}));
    EXPECT_EQ(status.modificationTime, (FileDateTime{200, 0}));
}

TEST_F(BuiltinCommandsTest, TouchStamp) {
    std::tm local{};
    local.tm_year = 2024 - 1900;
    local.tm_mon = 1 - 1;
    local.tm_mday = 2;
    local.tm_hour = 3;
    local.tm_min = 4;
    local.tm_sec = 5;
    const auto expected = SecondsFromLocalTime(local);
    ASSERT_TRUE(expected.has_value());

    const auto run = RunCaptured("touch", {"-t", "202401020304.05", "/notes.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    FileStatus status;
    ASSERT_EQ(root->Stat("/notes.txt", status), 0);
    EXPECT_EQ(status.accessTime, (FileDateTime{*expected, 0}));
    EXPECT_EQ(status.modificationTime, (FileDateTime{*expected, 0}));
}

TEST_F(BuiltinCommandsTest, TouchReference) {
    const FileDateTime fixed{1700000000, 0};
    ASSERT_EQ(root->SetTimes("/docs/a.md", fixed, fixed), 0);

    auto run = RunCaptured("touch", {"-r", "/docs/a.md", "/notes.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    FileStatus status;
    ASSERT_EQ(root->Stat("/notes.txt", status), 0);
    EXPECT_EQ(status.accessTime, fixed);
    EXPECT_EQ(status.modificationTime, fixed);

    // -d with -r: the string is relative to the reference's times.
    run = RunCaptured("touch", {"-r", "/docs/a.md", "-d", "+1 day", "/notes.txt"});
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    std::tm local = LocalTimeOf(fixed.seconds);
    local.tm_mday += 1;
    const auto plusADay = SecondsFromLocalTime(local);
    ASSERT_TRUE(plusADay.has_value());
    ASSERT_EQ(root->Stat("/notes.txt", status), 0);
    EXPECT_EQ(status.accessTime, (FileDateTime{*plusADay, 0}));
    EXPECT_EQ(status.modificationTime, (FileDateTime{*plusADay, 0}));

    run = RunCaptured("touch", {"-r", "/nothing", "/notes.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "touch: failed to get attributes of '/nothing': No such file or directory\n");
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, TouchErrors) {
    auto run = RunCaptured("touch", {"-t", "2024", "/notes.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "touch: invalid date format '2024'\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("touch", {"-d", "garbage", "/notes.txt"});
    EXPECT_EQ(run.err, "touch: invalid date format 'garbage'\n");
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("touch", {"-d", "@1", "-t", "202001010000", "/notes.txt"});
    EXPECT_EQ(run.err, "touch: cannot specify times from more than one source\n" + kTryTouch);
    EXPECT_EQ(run.status, 1);

    run = RunCaptured("touch", {"--time=bogus", "/notes.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err,
        "touch: invalid argument 'bogus' for '--time'\n"
        "Valid arguments are:\n"
        "  - 'atime', 'access', 'use'\n"
        "  - 'mtime', 'modify'\n" + kTryTouch);
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, TouchBuiltinPath) {
    const auto run = RunCaptured("touch", {"/bin/ls"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "touch: setting times of '/bin/ls': Permission denied\n");
    EXPECT_EQ(run.status, 1);
}