#include <gtest/gtest.h>
#include <regex>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"

namespace Haisos {
namespace {

// The tree every du test walks: /t/a/f1 of 5000 bytes (10 blocks of 512),
// /t/a/b/f2 of 100 (1 block), /t/c/e empty. Directories hold no blocks, so
// the numbers are the files' alone: a 5632 bytes under it, b 512, c 0.
void MakeDuTree(const std::shared_ptr<IFileSystem>& fs) {
    ASSERT_EQ(fs->CreateDirectory("/t", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/t/a", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/t/a/b", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/t/c", kDirMode), 0);
    auto write = [&](const std::string& path, const std::string& content) {
        auto file = fs->OpenFile(path, kFileOpenWriteCreateTruncate, kFileCreateMode);
        ASSERT_NE(file, nullptr) << path;
        file->Write(content.data(), content.size());
    };
    write("/t/a/f1", std::string(5000, 'x'));
    write("/t/a/b/f2", std::string(100, 'y'));
    write("/t/c/e", "");
}

} // namespace

TEST_F(BuiltinCommandsTest, DuListsDirectoriesPostOrder) {
    MakeDuTree(root);
    const auto captured = RunCaptured("du", {}, std::nullopt, "/t");
    EXPECT_EQ(captured.out, "1\t./a/b\n6\t./a\n0\t./c\n6\t.\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, DuAllSummarizeTotal) {
    MakeDuTree(root);
    auto captured = RunCaptured("du", {"-a", "."}, std::nullopt, "/t");
    EXPECT_EQ(captured.out,
        "1\t./a/b/f2\n"
        "1\t./a/b\n"
        "5\t./a/f1\n"
        "6\t./a\n"
        "0\t./c/e\n"
        "0\t./c\n"
        "6\t.\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("du", {"-s"}, std::nullopt, "/t");
    EXPECT_EQ(captured.out, "6\t.\n");

    captured = RunCaptured("du", {"-sc", "a", "c"}, std::nullopt, "/t");
    EXPECT_EQ(captured.out, "6\ta\n0\tc\n6\ttotal\n");

    captured = RunCaptured("du", {"-d", "1", "."}, std::nullopt, "/t");
    EXPECT_EQ(captured.out, "6\t./a\n0\t./c\n6\t.\n");

    captured = RunCaptured("du", {"--max-depth=0", "a"}, std::nullopt, "/t");
    EXPECT_EQ(captured.out, "6\ta\n");
}

TEST_F(BuiltinCommandsTest, DuUnits) {
    MakeDuTree(root);
    const auto du = [&](const std::vector<std::string>& args) {
        return RunCaptured("du", args, std::nullopt, "/t");
    };
    // -b is --apparent-size --block-size=1: the files' own sizes, directories 0.
    auto captured = du({"-b", "a"});
    EXPECT_EQ(captured.out, "100\ta/b\n5100\ta\n");

    captured = du({"--apparent-size", "a"});
    EXPECT_EQ(captured.out, "1\ta/b\n5\ta\n");

    captured = du({"-B", "1", "a/f1"});
    EXPECT_EQ(captured.out, "5120\ta/f1\n");

    // A -B value without a leading digit shows its suffix after each size,
    // the 1000-based k written lower case; with digits, the number alone.
    captured = du({"-B", "K", "a/f1"});
    EXPECT_EQ(captured.out, "5K\ta/f1\n");
    captured = du({"-B", "KB", "a/f1"});
    EXPECT_EQ(captured.out, "6kB\ta/f1\n");
    captured = du({"-B", "1KB", "a/f1"});
    EXPECT_EQ(captured.out, "6\ta/f1\n");

    captured = du({"-h", "a/f1"});
    EXPECT_EQ(captured.out, "5.0K\ta/f1\n");
    captured = du({"--si", "a/f1"});
    EXPECT_EQ(captured.out, "5.2k\ta/f1\n");

    captured = du({"-m", "a"});
    EXPECT_EQ(captured.out, "1\ta/b\n1\ta\n");

    // Size settings are applied in the order given, the last winning.
    captured = du({"-h", "-k", "a/f1"});
    EXPECT_EQ(captured.out, "5\ta/f1\n");
}

TEST_F(BuiltinCommandsTest, DuNullThresholdExclude) {
    MakeDuTree(root);
    const auto du = [&](const std::vector<std::string>& args) {
        return RunCaptured("du", args, std::nullopt, "/t");
    };
    // -0 ends each line with a NUL, which a C-string literal cannot hold.
    auto captured = du({"-0", "-s", "a"});
    EXPECT_EQ(captured.out, std::string("6\ta\0", 4));

    // -t hides lines below the threshold; the sizes still count into parents.
    captured = du({"-t", "2k", "-a", "a"});
    EXPECT_EQ(captured.out, "5\ta/f1\n6\ta\n");

    // An excluded entry is neither shown nor counted.
    captured = du({"--exclude=b", "a"});
    EXPECT_EQ(captured.out, "5\ta\n");
    captured = du({"--exclude=*/b", "a"});
    EXPECT_EQ(captured.out, "5\ta\n");

    // --inodes counts entries: 1 apiece, directories included.
    captured = du({"--inodes", "a"});
    EXPECT_EQ(captured.out, "2\ta/b\n4\ta\n");
}

TEST_F(BuiltinCommandsTest, DuTime) {
    MakeDuTree(root);
    const auto captured = RunCaptured("du", {"--time", "a/b"}, std::nullopt, "/t");
    // long-iso, the default --time-style: 1 block, when the subtree last
    // changed, the path.
    const std::regex expected("1\t[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}\ta/b\n");
    EXPECT_TRUE(std::regex_match(captured.out, expected)) << captured.out;
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, DuErrors) {
    MakeDuTree(root);
    const auto du = [&](const std::vector<std::string>& args) {
        return RunCaptured("du", args, std::nullopt, "/t");
    };
    auto captured = du({"missing"});
    EXPECT_EQ(captured.err, "du: cannot access 'missing': No such file or directory\n");
    EXPECT_EQ(captured.status, 1);

    captured = du({"-B", "x", "a"});
    EXPECT_EQ(captured.err, "du: invalid -B argument 'x'\n");
    EXPECT_EQ(captured.status, 1);
    captured = du({"-B", "1x", "a"});
    EXPECT_EQ(captured.err, "du: invalid suffix in -B argument '1x'\n");
    EXPECT_EQ(captured.status, 1);

    captured = du({"-d", "x", "a"});
    EXPECT_EQ(captured.err, "du: invalid maximum depth 'x'\nTry 'du --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    captured = du({"-s", "-a", "a"});
    EXPECT_EQ(captured.err,
        "du: cannot both summarize and show all entries\nTry 'du --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    captured = du({"--time=x", "a"});
    EXPECT_EQ(captured.err,
        "du: invalid argument 'x' for '--time'\n"
        "Valid arguments are:\n"
        "  - 'atime', 'access', 'use'\n"
        "  - 'ctime', 'status'\n"
        "Try 'du --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
}

} // namespace Haisos