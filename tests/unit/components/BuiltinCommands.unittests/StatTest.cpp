#include <gtest/gtest.h>
#include <regex>
#include <string>

#include "BuiltinCommandsFixture.h"
#include "src/components/Filesystem/FilesystemUtils.h"

using namespace Haisos;

namespace {

// One time line of stat's default layout, without its prefix: GNU's own
// "%Y-%m-%d %H:%M:%S.%N %z" through FormatDateTime, the host's zone deciding
// the offset, so only the shape can be compared.
const std::regex kTime("[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{9} [+-][0-9]{4}");

// Whether |line| is |prefix| followed by a time line.
bool IsTimeLine(const std::string& line, const std::string& prefix) {
    return line.size() > prefix.size() && line.compare(0, prefix.size(), prefix) == 0
        && std::regex_match(line.substr(prefix.size()), kTime);
}

} // namespace

// --- stat ---

TEST_F(BuiltinCommandsTest, StatDefaultLayoutOfAFile) {
    int status = -1;
    const Lines lines = Run("stat", {"/notes.txt"}, &status);
    EXPECT_EQ(status, 0);
    ASSERT_EQ(lines.size(), 8u);
    EXPECT_EQ(lines[0], "  File: /notes.txt");
    EXPECT_EQ(lines[1], "  Size: 17        \tBlocks: 1          IO Block: 4096   regular file");
    EXPECT_EQ(lines[2], "Device: 0,0\tInode: 0           Links: 1");
    EXPECT_EQ(lines[3], "Access: (0777/-rwxrwxrwx)  Uid: (    0/  haisos)   Gid: (    0/  haisos)");
    EXPECT_TRUE(IsTimeLine(lines[4], "Access: ")) << lines[4];
    EXPECT_TRUE(IsTimeLine(lines[5], "Modify: ")) << lines[5];
    EXPECT_TRUE(IsTimeLine(lines[6], "Change: ")) << lines[6];
    EXPECT_EQ(lines[7], " Birth: -");
}

TEST_F(BuiltinCommandsTest, StatDefaultLayoutOfADirectoryAndAnEmptyFile) {
    const Lines lines = Run("stat", {"/docs"});
    ASSERT_EQ(lines.size(), 8u);
    EXPECT_EQ(lines[0], "  File: /docs");
    EXPECT_EQ(lines[1], "  Size: 0         \tBlocks: 0          IO Block: 4096   directory");
    EXPECT_EQ(lines[2], "Device: 0,0\tInode: 0           Links: 3");
    EXPECT_EQ(lines[3], "Access: (0777/drwxrwxrwx)  Uid: (    0/  haisos)   Gid: (    0/  haisos)");
    EXPECT_TRUE(IsTimeLine(lines[4], "Access: ")) << lines[4];
    EXPECT_EQ(lines[7], " Birth: -");

    // A file of no content is an "empty file", as GNU's words it.
    WriteFile("/empty", "");
    const Lines empty = Run("stat", {"/empty"});
    ASSERT_EQ(empty.size(), 8u);
    EXPECT_EQ(empty[1], "  Size: 0         \tBlocks: 0          IO Block: 4096   regular empty file");
}

TEST_F(BuiltinCommandsTest, StatOfADevice) {
    root->Mount("/dev", factory->CreateServicesCreator()->CreateFileSystemService()->CreateDeviceFileSystem());
    const Lines lines = Run("stat", {"/dev/null"});
    ASSERT_EQ(lines.size(), 8u);
    EXPECT_EQ(lines[0], "  File: /dev/null");
    EXPECT_EQ(lines[1], "  Size: 0         \tBlocks: 0          IO Block: 4096   character special file");
    EXPECT_EQ(lines[2], "Device: 0,0\tInode: 0           Links: 1     Device type: 1,3");
    EXPECT_EQ(lines[3], "Access: (0777/crwxrwxrwx)  Uid: (    0/  haisos)   Gid: (    0/  haisos)");
}

TEST_F(BuiltinCommandsTest, StatFormatDirectives) {
    const Captured run = RunCaptured("stat",
        {"-c", "%n %s %b %B %F %A %a %h %i %u %U %g %G %w %W %d %D %o %f %m %C %N", "/notes.txt"});
    EXPECT_EQ(run.out,
        "/notes.txt 17 1 512 regular file -rwxrwxrwx 777 1 0 0 haisos 0 haisos - 0 0 0 4096 81ff / ? '/notes.txt'\n");
    EXPECT_EQ(run.err, "");
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, StatFormatWidthsAndPrecision) {
    const Captured run = RunCaptured("stat", {"-c", "%10s|%-12n|%.3n|%04a|%%|%E", "/notes.txt"});
    EXPECT_EQ(run.out, "        17|/notes.txt  |/no|0777|%|?\n");
    EXPECT_EQ(run.err, "");
    EXPECT_EQ(run.status, 0);

    // The epoch directives' precision truncates, never rounds, '.' with no
    // digits meaning nine. A width pads the whole text, and below the text's
    // own length still pads, trailing, once it leaves room for the seconds
    // (%-12.2Y) -- GNU 9.4's own rule, every case below checked against it.
    const FileDateTime fixed{1700000000, 500000000};
    ASSERT_EQ(root->SetTimes("/notes.txt", fixed, fixed), 0);
    const Captured epoch = RunCaptured("stat",
        {"-c", "%X %.Y %.3Y %.1Y %10.3Y|%-12.2Y|%15.3Y|%020.3Y", "/notes.txt"});
    EXPECT_EQ(epoch.out,
        "1700000000 1700000000.500000000 1700000000.500 1700000000.5 "
        "1700000000.500|1700000000.50 | 1700000000.500|0000001700000000.500\n");
    EXPECT_EQ(epoch.err, "");
    EXPECT_EQ(epoch.status, 0);
}

TEST_F(BuiltinCommandsTest, StatPrintfEscapesAndNoNewline) {
    // --printf adds no newline of its own and interprets backslash escapes.
    const Captured run = RunCaptured("stat", {"--printf", "%n\\t%s\\n", "/notes.txt", "/docs/a.md"});
    EXPECT_EQ(run.out, "/notes.txt\t17\n/docs/a.md\t5\n");
    EXPECT_EQ(run.err, "");
    EXPECT_EQ(run.status, 0);

    // An unrecognized escape warns on standard error and prints its character.
    const Captured unrecognized = RunCaptured("stat", {"--printf", "a\\q", "/notes.txt"});
    EXPECT_EQ(unrecognized.out, "aq");
    EXPECT_EQ(unrecognized.err, "stat: warning: unrecognized escape '\\q'\n");
    EXPECT_EQ(unrecognized.status, 0);

    // With -c a backslash is printed as is.
    const Captured literal = RunCaptured("stat", {"-c", "a\\tb", "/notes.txt"});
    EXPECT_EQ(literal.out, "a\\tb\n");
    EXPECT_EQ(literal.status, 0);
}

TEST_F(BuiltinCommandsTest, StatTerse) {
    FileStatus status;
    ASSERT_EQ(root->Stat("/notes.txt", status), 0);
    const std::string expected = "/notes.txt 17 1 81ff 0 0 0 0 1 0 0 "
        + std::to_string(status.accessTime.seconds) + " "
        + std::to_string(status.modificationTime.seconds) + " "
        + std::to_string(status.changeTime.seconds) + " 0 4096\n";
    const Captured run = RunCaptured("stat", {"-t", "/notes.txt"});
    EXPECT_EQ(run.out, expected);
    EXPECT_EQ(run.err, "");
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, StatFileSystem) {
    const Captured run = RunCaptured("stat", {"-f", "/notes.txt"});
    EXPECT_EQ(run.out,
        "  File: \"/notes.txt\"\n"
        "    ID: 0        Namelen: 255     Type: haisos\n"
        "Block size: 4096       Fundamental block size: 4096\n"
        "Blocks: Total: 0          Free: 0          Available: 0\n"
        "Inodes: Total: 0          Free: 0\n");
    EXPECT_EQ(run.err, "");
    EXPECT_EQ(run.status, 0);

    const Captured terse = RunCaptured("stat", {"-f", "-t", "/notes.txt"});
    EXPECT_EQ(terse.out, "/notes.txt 0 255 0 4096 4096 0 0 0 0 0\n");
    EXPECT_EQ(terse.status, 0);
}

TEST_F(BuiltinCommandsTest, StatErrors) {
    const Captured none = RunCaptured("stat", {});
    EXPECT_EQ(none.out, "");
    EXPECT_EQ(none.err, "stat: missing operand\nTry 'stat --help' for more information.\n");
    EXPECT_EQ(none.status, 1);

    // A failing operand is reported and gone past, as GNU's.
    const Captured missing = RunCaptured("stat", {"-c", "%s", "/notes.txt", "/missing"});
    EXPECT_EQ(missing.out, "17\n");
    EXPECT_EQ(missing.err, "stat: cannot statx '/missing': No such file or directory\n");
    EXPECT_EQ(missing.status, 1);

    const Captured fsMissing = RunCaptured("stat", {"-f", "/missing"});
    EXPECT_EQ(fsMissing.err,
        "stat: cannot read file system information for '/missing': No such file or directory\n");
    EXPECT_EQ(fsMissing.status, 1);

    // An invalid directive ends the command at once, what was printed
    // staying printed.
    const Captured invalid = RunCaptured("stat", {"-c", "x%5%y", "/notes.txt"});
    EXPECT_EQ(invalid.out, "x");
    EXPECT_EQ(invalid.err, "stat: '%5%': invalid directive\n");
    EXPECT_EQ(invalid.status, 1);
}