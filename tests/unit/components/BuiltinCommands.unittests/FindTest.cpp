#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "BuiltinCommand.h"
#include "BuiltinCommandList.h"
#include "BuiltinDate.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {
namespace {

// The tree every find test walks: /proj/a holding b/ (which holds empty)
// and the files x.txt and y.md, and /proj/z.h. Runs work in /proj, so that
// is where the relative starting points and the default "." are.
void MakeProj(const std::shared_ptr<IFileSystem>& fs) {
    ASSERT_EQ(fs->CreateDirectory("/proj", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/proj/a", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/proj/a/b", kDirMode), 0);
    auto write = [&](const std::string& path, const std::string& content) {
        auto file = fs->OpenFile(path, kFileOpenWriteCreateTruncate, kFileCreateMode);
        ASSERT_NE(file, nullptr) << path;
        file->Write(content.data(), content.size());
    };
    write("/proj/a/x.txt", "hi\n");
    write("/proj/a/b/empty", "");
    write("/proj/a/y.md", std::string(1100, 'y'));
    write("/proj/z.h", "h\n");
}

// The default listing of /proj: every file once, "." first, a parent before
// its children. The order among the entries of one directory is the
// filesystem's own (GNU's is readdir's), so tests compare that as a set.
const char* kProjTree =
    ".\n./a\n./a/b\n./a/b/empty\n./a/x.txt\n./a/y.md\n./z.h\n";

// Two outputs with the same lines, whatever order the filesystem listed
// each directory's entries in. The order find itself is responsible for (a
// parent before its children, -depth's, the starting points' own order) is
// asserted on its own.
void ExpectSameLines(const std::string& actual, const std::string& expected) {
    Lines left = SplitLines(actual);
    Lines right = SplitLines(expected);
    std::sort(left.begin(), left.end());
    std::sort(right.begin(), right.end());
    EXPECT_EQ(left, right);
}

// A line's place in a listing, for the order checks.
size_t PositionOf(const Lines& lines, const std::string& line) {
    return static_cast<size_t>(
        std::distance(lines.begin(), std::find(lines.begin(), lines.end(), line)));
}

// Sets a path's access and modification times now plus |offsetSeconds|
// (negative: that long ago). The change time becomes now, as utimensat's.
void SetFileTimes(const std::shared_ptr<IFileSystem>& fs, const std::string& path,
                  int64_t offsetSeconds) {
    FileDateTime when = CurrentFileDateTime();
    when.seconds += offsetSeconds;
    ASSERT_EQ(fs->SetTimes(path, when, when), 0) << path;
}

// True when the local time of day is within two minutes of midnight: the
// -daystart test's set is not stable there (files can land on yesterday or
// tomorrow).
bool NearLocalMidnight() {
    const std::tm local = LocalTimeOf(CurrentFileDateTime().seconds);
    const int secondsOfDay = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    return secondsOfDay < 120 || secondsOfDay > 86400 - 120;
}

} // namespace

TEST_F(BuiltinCommandsTest, FindPrintsTheTreeByDefault) {
    MakeProj(root);
    // No starting point: ".", no expression: -print. The starting point is
    // evaluated before its walk, so it is the first line.
    const auto captured = RunCaptured("find", {}, std::nullopt, "/proj");
    EXPECT_EQ(SplitLines(captured.out).front(), ".");
    ExpectSameLines(captured.out, kProjTree);
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    // An explicit "." is the same walk.
    const auto point = RunCaptured("find", {"."}, std::nullopt, "/proj");
    ExpectSameLines(point.out, kProjTree);
    EXPECT_EQ(point.status, 0);
}

TEST_F(BuiltinCommandsTest, FindStartingPointsKeepTheirSpelling) {
    MakeProj(root);
    // Trailing slashes are kept, and the children are built on what was
    // given: "a/" gives "a/x", "a//" gives "a//x".
    const auto one = RunCaptured("find", {"a/", "-maxdepth", "1"}, std::nullopt, "/proj");
    ExpectSameLines(one.out, "a/\na/b\na/x.txt\na/y.md\n");
    EXPECT_EQ(one.status, 0);
    const auto two = RunCaptured("find", {"a//", "-maxdepth", "1"}, std::nullopt, "/proj");
    ExpectSameLines(two.out, "a//\na//b\na//x.txt\na//y.md\n");
    const auto absolute = RunCaptured("find", {"/proj/z.h"}, std::nullopt, "/proj");
    EXPECT_EQ(absolute.out, "/proj/z.h\n");
}

TEST_F(BuiltinCommandsTest, FindMissingStartingPoint) {
    MakeProj(root);
    // A missing starting point is reported and gone past; the walk goes on.
    const auto captured = RunCaptured("find", {"a", "nope", "z.h"}, std::nullopt, "/proj");
    ExpectSameLines(captured.out, "a\na/b\na/b/empty\na/x.txt\na/y.md\nz.h\n");
    EXPECT_EQ(captured.err, "find: 'nope': No such file or directory\n");
    EXPECT_EQ(captured.status, 1);
    // An empty name is no file, not the working directory.
    const auto empty = RunCaptured("find", {""}, std::nullopt, "/proj");
    EXPECT_EQ(empty.out, "");
    EXPECT_EQ(empty.err, "find: '': No such file or directory\n");
    EXPECT_EQ(empty.status, 1);
    // The name is quoted as GNU's quote() has it.
    const auto quoted = RunCaptured("find", {"a'b"}, std::nullopt, "/proj");
    EXPECT_EQ(quoted.out, "");
    EXPECT_EQ(quoted.err, "find: 'a\\'b': No such file or directory\n");
    EXPECT_EQ(quoted.status, 1);
}

TEST_F(BuiltinCommandsTest, FindDepthOptions) {
    MakeProj(root);
    ExpectSameLines(RunCaptured("find", {"-maxdepth", "1"}, std::nullopt, "/proj").out,
        ".\n./a\n./z.h\n");
    ExpectSameLines(RunCaptured("find", {"-mindepth", "2", "-maxdepth", "2"}, std::nullopt, "/proj").out,
        "./a/b\n./a/x.txt\n./a/y.md\n");
    // -depth: the children before their parent, the root last.
    const auto depth = RunCaptured("find", {"-depth"}, std::nullopt, "/proj");
    ExpectSameLines(depth.out, kProjTree);
    const Lines depthLines = SplitLines(depth.out);
    EXPECT_EQ(depthLines.back(), ".");
    EXPECT_LT(PositionOf(depthLines, "./a/b/empty"), PositionOf(depthLines, "./a/b"));
    EXPECT_LT(PositionOf(depthLines, "./a/b"), PositionOf(depthLines, "./a"));
    EXPECT_EQ(depth.status, 0);
    // -d is -depth; its deprecation warning needs -warn, off with a file on
    // standard input.
    const auto d = RunCaptured("find", {"-d"}, std::nullopt, "/proj");
    ExpectSameLines(d.out, kProjTree);
    const Lines dLines = SplitLines(d.out);
    EXPECT_EQ(dLines.back(), ".");
    EXPECT_LT(PositionOf(dLines, "./a/b"), PositionOf(dLines, "./a"));
    EXPECT_EQ(d.err, "");
    EXPECT_EQ(d.status, 0);
}

TEST_F(BuiltinCommandsTest, FindNameAndPath) {
    MakeProj(root);
    EXPECT_EQ(RunCaptured("find", {"-name", "*.md"}, std::nullopt, "/proj").out,
        "./a/y.md\n");
    EXPECT_EQ(RunCaptured("find", {"-iname", "X*"}, std::nullopt, "/proj").out,
        "./a/x.txt\n");
    // A backslash escapes the next byte: "\a" is the name a.
    EXPECT_EQ(RunCaptured("find", {"-name", "\\a"}, std::nullopt, "/proj").out, "./a\n");
    // No kFnmPeriod: "*" matches the leading dot, so "." matches ".*".
    EXPECT_EQ(RunCaptured("find", {"-name", ".*", "-maxdepth", "1"}, std::nullopt, "/proj").out,
        ".\n");
    ExpectSameLines(RunCaptured("find", {"-path", "./a/*"}, std::nullopt, "/proj").out,
        "./a/b\n./a/b/empty\n./a/x.txt\n./a/y.md\n");
    EXPECT_EQ(RunCaptured("find", {"-ipath", "./A/X*"}, std::nullopt, "/proj").out,
        "./a/x.txt\n");
    EXPECT_EQ(RunCaptured("find", {"-wholename", "./a/b"}, std::nullopt, "/proj").out,
        "./a/b\n");
}

TEST_F(BuiltinCommandsTest, FindOperators) {
    MakeProj(root);
    ExpectSameLines(RunCaptured("find", {"-name", "x.txt", "-o", "-name", "z.h"}, std::nullopt, "/proj").out,
        "./a/x.txt\n./z.h\n");
    ExpectSameLines(RunCaptured("find", {"!", "-type", "d"}, std::nullopt, "/proj").out,
        "./a/b/empty\n./a/x.txt\n./a/y.md\n./z.h\n");
    ExpectSameLines(RunCaptured("find", {"-not", "-name", "*.*", "-type", "f"}, std::nullopt, "/proj").out,
        "./a/b/empty\n");
    ExpectSameLines(RunCaptured("find",
        {"(", "-name", "a", "-o", "-name", "b", ")", "-type", "d"}, std::nullopt, "/proj").out,
        "./a\n./a/b\n");
    // The comma's value is its right side alone.
    EXPECT_EQ(RunCaptured("find", {"-name", "a", ",", "-name", "b"}, std::nullopt, "/proj").out,
        "./a/b\n");
    ExpectSameLines(RunCaptured("find", {"-false", "-o", "-true"}, std::nullopt, "/proj").out,
        kProjTree);
}

TEST_F(BuiltinCommandsTest, FindImplicitPrint) {
    MakeProj(root);
    // -prune does not suppress the implicit print, but it holds the walk out
    // of ./a; the whole expression is (expr -a -print).
    ExpectSameLines(RunCaptured("find", {"-name", "a", "-prune", "-o", "-print"}, std::nullopt, "/proj").out,
        ".\n./z.h\n");
    // An explicit -print suppresses the implicit one.
    const auto explicitPrint = RunCaptured("find", {"-name", "a", "-print", "-name", "zz"},
        std::nullopt, "/proj");
    EXPECT_EQ(explicitPrint.out, "./a\n");
    EXPECT_EQ(explicitPrint.status, 0);
    // -quit never runs, so everything prints.
    ExpectSameLines(RunCaptured("find", {"-true", "-o", "-quit"}, std::nullopt, "/proj").out,
        kProjTree);
    // -quit ends the walk before the implicit print runs.
    const auto quit = RunCaptured("find", {"-name", "a", "-quit"}, std::nullopt, "/proj");
    EXPECT_EQ(quit.out, "");
    EXPECT_EQ(quit.status, 0);
}

TEST_F(BuiltinCommandsTest, FindPrint0) {
    MakeProj(root);
    const auto captured = RunCaptured("find", {"-name", "*.h", "-print0"}, std::nullopt, "/proj");
    EXPECT_EQ(captured.out, std::string("./z.h\0", 6));
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, FindPruneAndQuit) {
    MakeProj(root);
    // -prune lists ./a itself, but nothing below it.
    EXPECT_EQ(RunCaptured("find", {"-path", "./a", "-prune"}, std::nullopt, "/proj").out,
        "./a\n");
    const auto quit = RunCaptured("find", {"-print", "-quit"}, std::nullopt, "/proj");
    EXPECT_EQ(quit.out, ".\n");
    EXPECT_EQ(quit.status, 0);
}

TEST_F(BuiltinCommandsTest, FindTypeAndEmpty) {
    MakeProj(root);
    const char* files = "./a/b/empty\n./a/x.txt\n./a/y.md\n./z.h\n";
    ExpectSameLines(RunCaptured("find", {"-type", "f"}, std::nullopt, "/proj").out, files);
    ExpectSameLines(RunCaptured("find", {"-type", "d,f"}, std::nullopt, "/proj").out, kProjTree);
    // No symbolic links: -type l matches nothing, and that is no error.
    const auto none = RunCaptured("find", {"-type", "l"}, std::nullopt, "/proj");
    EXPECT_EQ(none.out, "");
    EXPECT_EQ(none.status, 0);
    EXPECT_EQ(RunCaptured("find", {"-empty"}, std::nullopt, "/proj").out, "./a/b/empty\n");
    // -xtype is -type (there are no links to follow).
    ExpectSameLines(RunCaptured("find", {"-xtype", "f"}, std::nullopt, "/proj").out, files);
    // The list argument's own errors, each as GNU wordsed it.
    EXPECT_EQ(RunCaptured("find", {"-type", "q"}, std::nullopt, "/proj").err,
        "find: Unknown argument to -type: q\n");
    EXPECT_EQ(RunCaptured("find", {"-type", "ff"}, std::nullopt, "/proj").err,
        "find: Must separate multiple arguments to -type using: ','\n");
    EXPECT_EQ(RunCaptured("find", {"-type", "f,f"}, std::nullopt, "/proj").err,
        "find: Duplicate file type 'f' in the argument list to -type.\n");
    EXPECT_EQ(RunCaptured("find", {"-type", ""}, std::nullopt, "/proj").err,
        "find: Arguments to -type should contain at least one letter\n");
    EXPECT_EQ(RunCaptured("find", {"-type", "q"}, std::nullopt, "/proj").status, 1);
}

TEST_F(BuiltinCommandsTest, FindSize) {
    MakeProj(root);
    ExpectSameLines(RunCaptured("find", {"-size", "+0", "-type", "f"}, std::nullopt, "/proj").out,
        "./a/x.txt\n./a/y.md\n./z.h\n");
    // The size is rounded up, so less than one unit is a zero: every empty
    // file, and the directories too (HaisosOS gives them size 0).
    ExpectSameLines(RunCaptured("find", {"-size", "-1k"}, std::nullopt, "/proj").out,
        ".\n./a\n./a/b\n./a/b/empty\n");
    EXPECT_EQ(RunCaptured("find", {"-size", "3c"}, std::nullopt, "/proj").out, "./a/x.txt\n");
    // No suffix: 512-byte blocks, so y.md (1100 bytes) is 3.
    EXPECT_EQ(RunCaptured("find", {"-size", "3"}, std::nullopt, "/proj").out, "./a/y.md\n");
    EXPECT_EQ(RunCaptured("find", {"-size", "+1k"}, std::nullopt, "/proj").out, "./a/y.md\n");
    // The unit's errors: an unknown suffix, no digits, a sign alone.
    EXPECT_EQ(RunCaptured("find", {"-size", "3q"}, std::nullopt, "/proj").err,
        "find: invalid -size type `q'\n");
    EXPECT_EQ(RunCaptured("find", {"-size", "k"}, std::nullopt, "/proj").err,
        "find: Invalid argument `k' to -size\n");
    EXPECT_EQ(RunCaptured("find", {"-size", "+"}, std::nullopt, "/proj").err,
        "find: invalid -size type `+'\n");
    EXPECT_EQ(RunCaptured("find", {"-size", "3q"}, std::nullopt, "/proj").status, 1);
}

TEST_F(BuiltinCommandsTest, FindTimes) {
    MakeProj(root);
    // y.md three days old, x.txt 30 seconds, z.h 90 seconds, empty 100
    // seconds into the future; the directories now.
    SetFileTimes(root, "/proj/a/y.md", -3 * 86400);
    SetFileTimes(root, "/proj/a/x.txt", -30);
    SetFileTimes(root, "/proj/z.h", -90);
    SetFileTimes(root, "/proj/a/b/empty", 100);
    // -mmin 1: the last minute, [0, 60).
    EXPECT_EQ(RunCaptured("find", {"-mmin", "1", "-type", "f"}, std::nullopt, "/proj").out,
        "./a/x.txt\n");
    // -mmin -2: newer than two minutes; a future time is newer than any age.
    ExpectSameLines(RunCaptured("find", {"-mmin", "-2", "-type", "f"}, std::nullopt, "/proj").out,
        "./a/b/empty\n./a/x.txt\n./z.h\n");
    // -mtime +1: more than two days old (GNU's window: N is [N, N+1) days).
    EXPECT_EQ(RunCaptured("find", {"-mtime", "+1", "-type", "f"}, std::nullopt, "/proj").out,
        "./a/y.md\n");
    ExpectSameLines(RunCaptured("find", {"-mtime", "0", "-type", "f"}, std::nullopt, "/proj").out,
        "./a/x.txt\n./z.h\n");
    ExpectSameLines(RunCaptured("find", {"-mtime", "-1", "-type", "f"}, std::nullopt, "/proj").out,
        "./a/b/empty\n./a/x.txt\n./z.h\n");
    // -daystart: ages from the start of tomorrow, so [0, 1 day) is today.
    if (NearLocalMidnight()) {
        GTEST_SKIP() << "too close to local midnight";
    }
    ExpectSameLines(RunCaptured("find", {"-daystart", "-mtime", "0", "-type", "f"}, std::nullopt, "/proj").out,
        "./a/b/empty\n./a/x.txt\n./z.h\n");
}

TEST_F(BuiltinCommandsTest, FindNewer) {
    MakeProj(root);
    // The tree's ages, on the same scale: the directories 200 seconds old,
    // x.txt 100, empty 50, y.md 10, z.h 5.
    SetFileTimes(root, "/proj", -200);
    SetFileTimes(root, "/proj/a", -200);
    SetFileTimes(root, "/proj/a/b", -200);
    SetFileTimes(root, "/proj/a/x.txt", -100);
    SetFileTimes(root, "/proj/a/b/empty", -50);
    SetFileTimes(root, "/proj/a/y.md", -10);
    SetFileTimes(root, "/proj/z.h", -5);
    // -newer: the modification times strictly later than the reference's.
    ExpectSameLines(RunCaptured("find", {"-newer", "a/x.txt"}, std::nullopt, "/proj").out,
        "./a/b/empty\n./a/y.md\n./z.h\n");
    // -anewer: the access times, which the SetTimes above set to the same
    // moments as the modification times.
    ExpectSameLines(RunCaptured("find", {"-anewer", "a/x.txt"}, std::nullopt, "/proj").out,
        "./a/b/empty\n./a/y.md\n./z.h\n");
    // -cnewer: the change times, which every SetTimes made now, so the whole
    // tree is newer than x.txt's modification time.
    ExpectSameLines(RunCaptured("find", {"-cnewer", "a/x.txt"}, std::nullopt, "/proj").out,
        kProjTree);
    // A date as the reference, and the forms GNU refuses.
    ExpectSameLines(RunCaptured("find", {"-newermt", "2000-01-01"}, std::nullopt, "/proj").out,
        kProjTree);
    const auto garbage = RunCaptured("find", {"-newermt", "garbage"}, std::nullopt, "/proj");
    EXPECT_EQ(garbage.err,
        "find: I cannot figure out how to interpret 'garbage' as a date or time\n");
    EXPECT_EQ(garbage.status, 1);
    const auto birth = RunCaptured("find", {"-newerBt", "x"}, std::nullopt, "/proj");
    EXPECT_EQ(birth.err,
        "find: This system does not provide a way to find the birth time of a file.\n"
        "find: invalid predicate `-newerBt'\n");
    EXPECT_EQ(birth.status, 1);
    const auto badLetters = RunCaptured("find", {"-newerqm", "a"}, std::nullopt, "/proj");
    EXPECT_EQ(badLetters.err, "find: invalid predicate `-newerqm'\n");
    EXPECT_EQ(badLetters.status, 1);
    const auto missing = RunCaptured("find", {"-newer", "nope"}, std::nullopt, "/proj");
    EXPECT_EQ(missing.err, "find: 'nope': No such file or directory\n");
    EXPECT_EQ(missing.status, 1);
}

TEST_F(BuiltinCommandsTest, FindPermAndOwners) {
    MakeProj(root);
    // Every file's mode is 0777.
    EXPECT_EQ(RunCaptured("find", {"-perm", "777", "-maxdepth", "0"}, std::nullopt, "/proj").out,
        ".\n");
    EXPECT_EQ(RunCaptured("find", {"-perm", "644"}, std::nullopt, "/proj").out, "");
    EXPECT_EQ(RunCaptured("find", {"-perm", "-u=rwx,g+r", "-maxdepth", "0"}, std::nullopt, "/proj").out,
        ".\n");
    EXPECT_EQ(RunCaptured("find", {"-perm", "/222", "-maxdepth", "0"}, std::nullopt, "/proj").out,
        ".\n");
    // /0 matches everything, with GNU's change-of-meaning warning.
    const auto anyZero = RunCaptured("find", {"-perm", "/0", "-maxdepth", "0"}, std::nullopt, "/proj");
    EXPECT_EQ(anyZero.out, ".\n");
    EXPECT_EQ(anyZero.err,
        "find: warning: you have specified a mode pattern /0 (which is equivalent to /000)."
        " The meaning of -perm /000 has now been changed to be consistent with -perm -000;"
        " that is, while it used to match no files, it now matches all files.\n");
    EXPECT_EQ(anyZero.status, 0);
    EXPECT_EQ(RunCaptured("find", {"-perm", "g+q"}, std::nullopt, "/proj").err,
        "find: invalid mode 'g+q'\n");
    EXPECT_EQ(RunCaptured("find", {"-perm", "+222"}, std::nullopt, "/proj").err,
        "find: invalid mode '+222'\n");
    EXPECT_EQ(RunCaptured("find", {"-perm", "g+q"}, std::nullopt, "/proj").status, 1);
    // An empty permission list is a valid clause ("u=" clears, "=" is 0);
    // an empty mode is not.
    EXPECT_EQ(RunCaptured("find", {"-perm", "-u=", "-maxdepth", "0"}, std::nullopt, "/proj").out,
        ".\n");
    const auto equalsOnly = RunCaptured("find", {"-perm", "=", "-maxdepth", "0"}, std::nullopt, "/proj");
    EXPECT_EQ(equalsOnly.out, "");
    EXPECT_EQ(equalsOnly.err, "");
    EXPECT_EQ(equalsOnly.status, 0);
    EXPECT_EQ(RunCaptured("find", {"-perm", "-"}, std::nullopt, "/proj").err,
        "find: invalid mode '-'\n");
    EXPECT_EQ(RunCaptured("find", {"-perm", ""}, std::nullopt, "/proj").err,
        "find: invalid mode ''\n");
    // Every file is owned by haisos, uid and gid 0.
    EXPECT_EQ(RunCaptured("find", {"-user", "haisos", "-maxdepth", "0"}, std::nullopt, "/proj").out,
        ".\n");
    EXPECT_EQ(RunCaptured("find", {"-uid", "0", "-maxdepth", "0"}, std::nullopt, "/proj").out,
        ".\n");
    EXPECT_EQ(RunCaptured("find", {"-gid", "+1"}, std::nullopt, "/proj").out, "");
    EXPECT_EQ(RunCaptured("find", {"-nouser"}, std::nullopt, "/proj").out, "");
    const auto unknownUser = RunCaptured("find", {"-user", "nosuch"}, std::nullopt, "/proj");
    EXPECT_EQ(unknownUser.err, "find: 'nosuch' is not the name of a known user\n");
    EXPECT_EQ(unknownUser.status, 1);
    // A numeric ID is digits alone: a sign makes it a name.
    EXPECT_EQ(RunCaptured("find", {"-user", "+5"}, std::nullopt, "/proj").err,
        "find: '+5' is not the name of a known user\n");
    EXPECT_EQ(RunCaptured("find", {"-group", "-5"}, std::nullopt, "/proj").err,
        "find: '-5' is not the name of an existing group\n");
    EXPECT_EQ(RunCaptured("find", {"-group", "0", "-maxdepth", "0"}, std::nullopt, "/proj").out,
        ".\n");
}

TEST_F(BuiltinCommandsTest, FindLinksInumSamefile) {
    MakeProj(root);
    // A directory's link count is 2 plus its subdirectories, so . and ./a
    // have 3, ./a/b has 2.
    ExpectSameLines(RunCaptured("find", {"-links", "+2", "-type", "d"}, std::nullopt, "/proj").out,
        ".\n./a\n");
    EXPECT_EQ(RunCaptured("find", {"-inum", "0", "-maxdepth", "0"}, std::nullopt, "/proj").out,
        ".\n");
    EXPECT_EQ(RunCaptured("find", {"-samefile", "a/x.txt"}, std::nullopt, "/proj").out,
        "./a/x.txt\n");
    const auto missing = RunCaptured("find", {"-samefile", "nope"}, std::nullopt, "/proj");
    EXPECT_EQ(missing.err, "find: 'nope': No such file or directory\n");
    EXPECT_EQ(missing.status, 1);
    // No links: -lname never matches.
    EXPECT_EQ(RunCaptured("find", {"-lname", "x"}, std::nullopt, "/proj").out, "");
    EXPECT_EQ(RunCaptured("find", {"-readable", "-maxdepth", "0"}, std::nullopt, "/proj").out,
        ".\n");
}

TEST_F(BuiltinCommandsTest, FindRegex) {
    MakeProj(root);
    // The whole path must match, leftmost-longest making it exact.
    EXPECT_EQ(RunCaptured("find", {"-regex", ".*x\\.txt"}, std::nullopt, "/proj").out,
        "./a/x.txt\n");
    EXPECT_EQ(RunCaptured("find", {"-regex", "x.*"}, std::nullopt, "/proj").out, "");
    // The default is emacs: a + unescaped is one-or-more, a backslashed one
    // a literal, and \| alternates.
    EXPECT_EQ(RunCaptured("find", {"-regex", ".+x.*"}, std::nullopt, "/proj").out,
        "./a/x.txt\n");
    EXPECT_EQ(RunCaptured("find", {"-regex", ".\\+x.*"}, std::nullopt, "/proj").out, "");
    ExpectSameLines(RunCaptured("find", {"-regex", "./a\\|./a/b"}, std::nullopt, "/proj").out,
        "./a\n./a/b\n");
    ExpectSameLines(RunCaptured("find",
        {"-regextype", "posix-extended", "-regex", ".*(x|z)\\..*"}, std::nullopt, "/proj").out,
        "./a/x.txt\n./z.h\n");
    EXPECT_EQ(RunCaptured("find", {"-iregex", ".*X\\.TXT"}, std::nullopt, "/proj").out,
        "./a/x.txt\n");
    // Compile failures, with GNU's message and its raw-quoted pattern; an
    // unterminated bracket is "Invalid regular expression", not the engine's
    // own wording.
    const auto bracket = RunCaptured("find", {"-regex", "["}, std::nullopt, "/proj");
    EXPECT_EQ(bracket.err,
        "find: failed to compile regular expression '[': Invalid regular expression\n");
    EXPECT_EQ(bracket.status, 1);
    const auto paren = RunCaptured("find", {"-regex", "\\("}, std::nullopt, "/proj");
    EXPECT_EQ(paren.err,
        "find: failed to compile regular expression '\\(': Unmatched ( or \\(\n");
    EXPECT_EQ(paren.status, 1);
    // An unknown -regextype, with the whole valid list on one line.
    const auto badType = RunCaptured("find", {"-regextype", "foo"}, std::nullopt, "/proj");
    EXPECT_EQ(badType.err,
        "find: Unknown regular expression type 'foo'; valid types are"
        " 'findutils-default', 'ed', 'emacs', 'gnu-awk', 'grep', 'posix-awk',"
        " 'awk', 'posix-basic', 'posix-egrep', 'egrep', 'posix-extended',"
        " 'posix-minimal-basic', 'sed'.\n");
    EXPECT_EQ(badType.status, 1);
    // sed is accepted, matched by the Basic engine, and reported not treated.
    const auto sed = RunCaptured("find", {"-regextype", "sed", "-regex", ".*"}, std::nullopt, "/proj");
    ExpectSameLines(sed.out, kProjTree);
    EXPECT_EQ(sed.err,
        "Parameter -regextype sed is not treated by HaisosOS find v. 1.0.0\n");
    EXPECT_EQ(sed.status, 0);
}

TEST_F(BuiltinCommandsTest, FindExpressionErrors) {
    MakeProj(root);
    const auto run = [this](const std::vector<std::string>& args) {
        return RunCaptured("find", args, std::nullopt, "/proj");
    };
    // An unknown predicate.
    EXPECT_EQ(run({"-foo"}).err, "find: unknown predicate `-foo'\n");
    EXPECT_EQ(run({"-foo"}).status, 1);
    // A path after the expression has begun, with the hint when it names
    // something (the working directory holds a "."), without one when it
    // does not.
    EXPECT_EQ(run({"-name", "x", "."}).err,
        "find: paths must precede expression: `.'\n"
        "find: possible unquoted pattern after predicate `-name'?\n");
    EXPECT_EQ(run({".", "-empty", "extra"}).err,
        "find: paths must precede expression: `extra'\n");
    // Missing arguments.
    EXPECT_EQ(run({".", "-name"}).err, "find: missing argument to `-name'\n");
    EXPECT_EQ(run({".", "-newer"}).err, "find: missing argument to `-newer'\n");
    EXPECT_EQ(run({".", "-newermt"}).err, "find: The '-newermt' test needs an argument\n");
    // Malformed numbers, each in its predicate's own wording.
    EXPECT_EQ(run({".", "-maxdepth", "-1"}).err,
        "find: Expected a positive decimal integer argument to -maxdepth, but got '-1'\n");
    EXPECT_EQ(run({".", "-maxdepth", "x"}).err,
        "find: Expected a positive decimal integer argument to -maxdepth, but got 'x'\n");
    EXPECT_EQ(run({".", "-mtime", "x"}).err, "find: invalid argument `x' to `-mtime'\n");
    EXPECT_EQ(run({".", "-mtime", "1x"}).err, "find: invalid argument `1x' to `-mtime'\n");
    EXPECT_EQ(run({".", "-uid", "x"}).err, "find: invalid argument `x' to `-uid'\n");
    EXPECT_EQ(run({".", "-links", "x"}).err, "find: invalid argument `x' to `-links'\n");
    EXPECT_EQ(run({".", "-inum", "x"}).err, "find: invalid argument `x' to `-inum'\n");
    EXPECT_EQ(run({".", "-used", "x"}).err, "find: Invalid argument x to -used\n");
    EXPECT_EQ(run({".", "-context", "x"}).err,
        "find: invalid predicate -context: SELinux is not enabled.\n");
    EXPECT_EQ(run({".", "-maxdepth", "-1"}).status, 1);
    // The tree the operators build, and its own errors.
    EXPECT_EQ(run({"-a"}).err,
        "find: invalid expression; you have used a binary operator '-a' with nothing before it.\n");
    EXPECT_EQ(run({".", "-a"}).err,
        "find: invalid expression; you have used a binary operator '-a' with nothing before it.\n");
    EXPECT_EQ(run({".", "("}).err,
        "find: invalid expression; expected to find a ')' but didn't see one."
        " Perhaps you need an extra predicate after '('\n");
    EXPECT_EQ(run({".", "-true", "-a"}).err, "find: expected an expression after '-a'\n");
    EXPECT_EQ(run({".", "!"}).err, "find: expected an expression after '!'\n");
    EXPECT_EQ(run({".", "-true", ")"}).err, "find: you have too many ')'\n");
    EXPECT_EQ(run({"(", "-not", ")"}).err, "find: expected an expression between '-not' and ')'\n");
    EXPECT_EQ(run({"(", "-true", "-a", ")"}).err,
        "find: expected an expression between '-a' and ')'\n");
    EXPECT_EQ(run({"(", ",", ")"}).err,
        "find: invalid expression; you have used a binary operator ',' with nothing before it.\n");
    EXPECT_EQ(run({"(", "-true", ")", ")"}).err, "find: you have too many ')'\n");
    // The warnings, each needing -warn with the standard input a file.
    const auto global = run({"-warn", "-name", "x", "-maxdepth", "1"});
    EXPECT_EQ(global.err,
        "find: warning: you have specified the global option -maxdepth after the argument"
        " -name, but global options are not positional, i.e., -maxdepth affects tests"
        " specified before it as well as those specified after it."
        "  Please specify global options before other arguments.\n");
    EXPECT_EQ(global.status, 0);
    const auto globalAfterType = run({"-warn", "-type", "f", "-name", "x", "-maxdepth", "1"});
    EXPECT_EQ(globalAfterType.err,
        "find: warning: you have specified the global option -maxdepth after the argument"
        " -type, but global options are not positional, i.e., -maxdepth affects tests"
        " specified before it as well as those specified after it."
        "  Please specify global options before other arguments.\n");
    // -d after a test warns twice: its deprecation first, as GNU's.
    const auto d = run({"-warn", "-name", "x", "-d"});
    EXPECT_EQ(d.err,
        "find: warning: the -d option is deprecated; please use -depth instead,"
        " because the latter is a POSIX-compliant feature.\n"
        "find: warning: you have specified the global option -d after the argument -name,"
        " but global options are not positional, i.e., -d affects tests specified before"
        " it as well as those specified after it.  Please specify global options before"
        " other arguments.\n");
    EXPECT_EQ(run({"-warn", "-name", "x/y"}).err,
        "find: warning: '-name' matches against basenames only, but the given pattern"
        " contains a directory separator ('/'), thus the expression will evaluate to false"
        " all the time.  Did you mean '-wholename'?\n");
    // -path's trailing-slash warning is unconditional.
    EXPECT_EQ(run({".", "-path", "./a/"}).err,
        "find: warning: -path ./a/ will not match anything because it ends with /.\n");
    EXPECT_EQ(run({".", "-path", "./a/"}).status, 0);
}

TEST_F(BuiltinCommandsTest, FindLeadingOptions) {
    MakeProj(root);
    // -H, -L and -P change nothing; -O3 and -D stat are reported not treated.
    const auto captured = RunCaptured("find",
        {"-H", "-L", "-P", "-O3", "-D", "stat", ".", "-maxdepth", "0"}, std::nullopt, "/proj");
    EXPECT_EQ(captured.out, ".\n");
    EXPECT_EQ(captured.err,
        "Parameter -O3 is not treated by HaisosOS find v. 1.0.0\n"
        "Parameter -D is not treated by HaisosOS find v. 1.0.0\n");
    EXPECT_EQ(captured.status, 0);
    // "--" ends the leading options; "." is a starting point again.
    const auto dashDash = RunCaptured("find", {"--", ".", "-maxdepth", "0"}, std::nullopt, "/proj");
    EXPECT_EQ(dashDash.out, ".\n");
    EXPECT_EQ(dashDash.err, "");
    EXPECT_EQ(dashDash.status, 0);
    // -D without its argument is GNU's error, with the Try line.
    const auto noD = RunCaptured("find", {"-D"}, std::nullopt, "/proj");
    EXPECT_EQ(noD.err,
        "find: Missing argument after the -D option.\n"
        "Try 'find --help' for more information.\n");
    EXPECT_EQ(noD.status, 1);
}

TEST_F(BuiltinCommandsTest, FindFiles0From) {
    MakeProj(root);
    WriteFile("/proj/list0", std::string("a\0z.h\0", 6));
    WriteFile("/proj/bad0", std::string("a\0\0", 3));
    // The names are walked one at a time, in the order they are read.
    const auto fromFile = RunCaptured("find", {"-files0-from", "list0", "-maxdepth", "0"},
        std::nullopt, "/proj");
    EXPECT_EQ(fromFile.out, "a\nz.h\n");
    EXPECT_EQ(fromFile.err, "");
    EXPECT_EQ(fromFile.status, 0);
    // "-" is the standard input.
    const auto fromStdin = RunCaptured("find", {"-files0-from", "-", "-maxdepth", "0"},
        std::string("a\0", 2), "/proj");
    EXPECT_EQ(fromStdin.out, "a\n");
    EXPECT_EQ(fromStdin.err, "");
    EXPECT_EQ(fromStdin.status, 0);
    // A starting point on the command line cannot combine with it.
    const auto both = RunCaptured("find", {".", "-files0-from", "list0"}, std::nullopt, "/proj");
    EXPECT_EQ(both.err,
        "find: extra operand '.'\n"
        "find: file operands cannot be combined with -files0-from\n");
    EXPECT_EQ(both.status, 1);
    const auto missing = RunCaptured("find", {"-files0-from", "nofile"}, std::nullopt, "/proj");
    EXPECT_EQ(missing.err,
        "find: cannot open 'nofile' for reading: No such file or directory\n");
    EXPECT_EQ(missing.status, 1);
    const auto directory = RunCaptured("find", {"-files0-from", "a"}, std::nullopt, "/proj");
    EXPECT_EQ(directory.err, "find: 'a': read error: Is a directory\n");
    EXPECT_EQ(directory.status, 1);
    // A zero-length name is reported with its 1-based number; the names
    // before it were searched.
    const auto empty = RunCaptured("find", {"-files0-from", "bad0", "-maxdepth", "0"},
        std::nullopt, "/proj");
    EXPECT_EQ(empty.out, "a\n");
    EXPECT_EQ(empty.err, "find: 'bad0':2: invalid zero-length file name\n");
    EXPECT_EQ(empty.status, 1);
}

TEST_F(BuiltinCommandsTest, FindHelpAndVersion) {
    MakeProj(root);
    const auto help = RunCaptured("find", {"--help"}, std::nullopt, "/proj");
    EXPECT_EQ(help.out, BuiltinHelpText(*CreateFindCommand()));
    EXPECT_EQ(help.err, "");
    EXPECT_EQ(help.status, 0);
    // -help in the expression stops the parse and ends with exit 0 too.
    const auto inExpression = RunCaptured("find", {".", "-help"}, std::nullopt, "/proj");
    EXPECT_EQ(inExpression.out, BuiltinHelpText(*CreateFindCommand()));
    EXPECT_EQ(inExpression.status, 0);
    const auto version = RunCaptured("find", {"-version"}, std::nullopt, "/proj");
    EXPECT_EQ(version.out, "find (HaisosOS builtin) 1.0.0\n");
    EXPECT_EQ(version.status, 0);
}

TEST_F(BuiltinCommandsTest, FindIsStoppedPromptly) {
    // A tree large enough that the walk cannot finish before the stop lands.
    ASSERT_EQ(root->CreateDirectory("/big", kDirMode), 0);
    for (int i = 0; i < 2000; ++i) {
        WriteFile("/big/f" + std::to_string(i), "x");
    }
    StartProcessOptions options;
    options.stdOut = streams->OpenFile("/findout", kFileOpenWriteCreateTruncate, kFileCreateMode);
    options.stdErr = streams->OpenFile("/finderr", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(options.stdOut, nullptr);
    ASSERT_NE(options.stdErr, nullptr);
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/find",
        {"/big"}, "/", options);
    ASSERT_NE(process, nullptr);
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(1000));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(process->ExitCode().value(), 143);
}

} // namespace Haisos