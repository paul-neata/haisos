#include <gtest/gtest.h>
#include "PhysicalFileSystem.h"
#include "Filesystem.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <windows.h>
#undef CreateDirectory
#undef RemoveDirectory
#undef GetCurrentDirectory
#undef small

#define unlink _unlink
#define rmdir _rmdir

#ifndef S_IRWXU
#define S_IRWXU (_S_IREAD | _S_IWRITE | _S_IEXEC)
#endif
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#endif
#ifndef S_IWUSR
#define S_IWUSR _S_IWRITE
#endif
#else
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

using namespace Haisos;

namespace {

#ifdef _WIN32
const std::string kRootDir = []() {
    char buf[MAX_PATH];
    DWORD len = GetTempPathA(MAX_PATH, buf);
    return std::string(buf, len) + "haisos_pfs_test_root";
}();
#else
const std::string kRootDir = "/tmp/haisos_pfs_test_root";
#endif

class PhysicalFileSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        ::unlink((kRootDir + "/inside.txt").c_str());
        ::rmdir(kRootDir.c_str());
        auto fs = FileSystem::Create();
        ASSERT_EQ(fs->CreateDirectory(kRootDir, S_IRWXU), 0);
    }

    void TearDown() override {
        ::unlink((kRootDir + "/inside.txt").c_str());
        ::rmdir(kRootDir.c_str());
    }
};

TEST_F(PhysicalFileSystemTest, WriteAndReadWithinRootSucceeds) {
    auto fs = PhysicalFileSystem::Create(kRootDir);

    int fd = fs->OpenFile("inside.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_GE(fd, 0);
    const char* data = "hello";
    EXPECT_EQ(fs->WriteFile(fd, data, std::strlen(data)), static_cast<ssize_t>(std::strlen(data)));
    fs->CloseFile(fd);

    fd = fs->OpenFile("inside.txt", O_RDONLY);
    ASSERT_GE(fd, 0);
    char buf[16] = {};
    ssize_t n = fs->ReadFile(fd, buf, sizeof(buf) - 1);
    fs->CloseFile(fd);
    EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), "hello");
}

// "/inside.txt" is the file in the root on every platform. On Windows a path
// that starts with '/' but names no drive is not absolute, and it used to be
// joined to the root as it was -- which names the top of the drive -- so every
// absolute path was refused as escaping the root.
TEST_F(PhysicalFileSystemTest, AnAbsolutePathIsTakenWithinTheRoot) {
    auto fs = PhysicalFileSystem::Create(kRootDir);

    int fd = fs->OpenFile("/inside.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(fs->WriteFile(fd, "x", 1), 1);
    fs->CloseFile(fd);
    EXPECT_TRUE(std::filesystem::exists(kRootDir + "/inside.txt"));

    FileStatus status;
    EXPECT_EQ(fs->Stat("/inside.txt", status), 0);
    ASSERT_EQ(fs->Stat("/", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
}

// A root at the top of a disk ("/", or "C:\" on Windows) holds every path on
// it. It ends in a separator already, and the check that a path lies within
// the root used to demand one more after it, refusing everything.
TEST_F(PhysicalFileSystemTest, ARootAtTheTopOfTheDiskHoldsEveryPath) {
    std::filesystem::path temp = std::filesystem::temp_directory_path();
    if (!temp.has_filename()) {
        temp = temp.parent_path();
    }
    auto fs = PhysicalFileSystem::Create(temp.root_path().string());

    FileStatus status;
    ASSERT_EQ(fs->Stat("/" + temp.relative_path().generic_string(), status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
}

TEST_F(PhysicalFileSystemTest, TraversalOutsideRootIsRejected) {
    auto fs = PhysicalFileSystem::Create(kRootDir);

    int fd = fs->OpenFile("../outside.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_EQ(fd, -1);
}

TEST_F(PhysicalFileSystemTest, ReadDirectoryListsCreatedFile) {
    auto fs = PhysicalFileSystem::Create(kRootDir);

    int fd = fs->OpenFile("inside.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_GE(fd, 0);
    fs->CloseFile(fd);

    auto entries = fs->ReadDirectory(".");
    bool found = false;
    for (const auto& entry : entries) {
        if (entry.name == "inside.txt") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(PhysicalFileSystemTest, RemoveFileRemovesAFileButNotADirectory) {
    auto fs = PhysicalFileSystem::Create(kRootDir);

    int fd = fs->OpenFile("inside.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_GE(fd, 0);
    fs->CloseFile(fd);

    EXPECT_EQ(fs->RemoveFile("inside.txt"), 0);
    EXPECT_LT(fs->OpenFile("inside.txt", O_RDONLY), 0);
    EXPECT_LT(fs->RemoveFile("inside.txt"), 0);
    EXPECT_LT(fs->RemoveFile("../outside.txt"), 0);
}

} // namespace

#ifndef _WIN32
// --- Symbolic links already on the disk ---
//
// Nothing done through a PhysicalFileSystem can create a link, but the
// directory it is jailed to may hold some. The jail used to canonicalize with
// weakly_canonical alone, which cannot resolve a link that leads nowhere: a
// path ending in a dangling link passed the check unresolved, and open() with
// O_CREAT then created the link's target, wherever it pointed. Removal
// resolved the last component through links as well, so removing a link
// removed what it pointed at.

namespace {

// A root to jail a filesystem to, next to a directory outside it, both in a
// directory of their own under the system's temporary directory.
class PhysicalFileSystemLinkTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test = ::testing::UnitTest::GetInstance()->current_test_info();
        m_base = std::filesystem::temp_directory_path() /
            ("haisos_pfs_links_" + std::to_string(::getpid()) + "_" + test->name());
        std::filesystem::remove_all(m_base);
        std::filesystem::create_directories(Root());
        std::filesystem::create_directories(Outside());
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(m_base, ec);
    }

    std::filesystem::path Root() const { return m_base / "root"; }
    std::filesystem::path Outside() const { return m_base / "outside"; }

    static void WriteHostFile(const std::filesystem::path& path, const std::string& content) {
        std::ofstream(path) << content;
    }

    std::filesystem::path m_base;
};

} // namespace

TEST_F(PhysicalFileSystemLinkTest, NothingIsCreatedThroughADanglingLinkToOutsideTheRoot) {
    std::filesystem::create_symlink(Outside() / "created.txt", Root() / "notes.txt");
    auto fs = PhysicalFileSystem::Create(Root().string());

    EXPECT_EQ(fs->OpenFile("/notes.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR), -1);
    EXPECT_EQ(fs->OpenFile("notes.txt", O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR), -1);
    // A ".." cannot hide the link from the check either.
    EXPECT_EQ(fs->OpenFile("/missing/../notes.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR), -1);
    EXPECT_FALSE(std::filesystem::exists(Outside() / "created.txt"));
    // The link itself is left as it was.
    EXPECT_TRUE(std::filesystem::is_symlink(std::filesystem::symlink_status(Root() / "notes.txt")));
}

// Where a dangling link points cannot be known without following it, so one
// pointing inside the root is refused all the same.
TEST_F(PhysicalFileSystemLinkTest, ADanglingLinkPointingInsideIsRefusedToo) {
    std::filesystem::create_symlink(Root() / "nothing.txt", Root() / "inner.txt");
    auto fs = PhysicalFileSystem::Create(Root().string());

    EXPECT_EQ(fs->OpenFile("inner.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR), -1);
    EXPECT_FALSE(std::filesystem::exists(Root() / "nothing.txt"));
}

TEST_F(PhysicalFileSystemLinkTest, NoDirectoryIsCreatedThroughADanglingLink) {
    std::filesystem::create_symlink(Outside() / "newdir", Root() / "dir");
    auto fs = PhysicalFileSystem::Create(Root().string());

    EXPECT_EQ(fs->CreateDirectory("dir", S_IRWXU), -1);
    EXPECT_EQ(fs->CreateDirectory("dir/sub", S_IRWXU), -1);
    EXPECT_FALSE(std::filesystem::exists(Outside() / "newdir"));
}

TEST_F(PhysicalFileSystemLinkTest, ALinkToOutsideTheRootCannotBeReadEvenBehindDotDot) {
    WriteHostFile(Outside() / "secret.txt", "secret");
    std::filesystem::create_symlink(Outside() / "secret.txt", Root() / "secret_link");
    auto fs = PhysicalFileSystem::Create(Root().string());

    EXPECT_EQ(fs->OpenFile("secret_link", O_RDONLY), -1);
    // weakly_canonical stops at the first component that does not exist and
    // only normalizes the rest, so "missing/../secret_link" once came out as
    // the unresolved link.
    EXPECT_EQ(fs->OpenFile("missing/../secret_link", O_RDONLY), -1);
}

TEST_F(PhysicalFileSystemLinkTest, ALinkWithinTheRootIsFollowed) {
    WriteHostFile(Root() / "target.txt", "inside");
    std::filesystem::create_symlink(Root() / "target.txt", Root() / "link.txt");
    auto fs = PhysicalFileSystem::Create(Root().string());

    int fd = fs->OpenFile("link.txt", O_RDONLY);
    ASSERT_GE(fd, 0);
    char buf[16] = {};
    ssize_t n = fs->ReadFile(fd, buf, sizeof(buf) - 1);
    fs->CloseFile(fd);
    EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), "inside");
}

TEST_F(PhysicalFileSystemLinkTest, RemoveFileRemovesALinkNotWhatItPointsAt) {
    WriteHostFile(Outside() / "keep.txt", "keep");
    WriteHostFile(Root() / "target.txt", "target");
    std::filesystem::create_symlink(Outside() / "keep.txt", Root() / "out_link");
    std::filesystem::create_symlink(Root() / "target.txt", Root() / "in_link");
    std::filesystem::create_symlink(Outside() / "nowhere", Root() / "dangling_link");
    auto fs = PhysicalFileSystem::Create(Root().string());

    EXPECT_EQ(fs->RemoveFile("out_link"), 0);
    EXPECT_EQ(fs->RemoveFile("in_link"), 0);
    EXPECT_EQ(fs->RemoveFile("dangling_link"), 0);
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(Root() / "out_link")));
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(Root() / "in_link")));
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(Root() / "dangling_link")));
    EXPECT_TRUE(std::filesystem::exists(Outside() / "keep.txt"));
    EXPECT_TRUE(std::filesystem::exists(Root() / "target.txt"));
}

TEST_F(PhysicalFileSystemLinkTest, RemoveDirectoryNeverRemovesThroughALink) {
    std::filesystem::create_directories(Root() / "empty_dir");
    std::filesystem::create_symlink(Root() / "empty_dir", Root() / "dir_link");
    auto fs = PhysicalFileSystem::Create(Root().string());

    EXPECT_EQ(fs->RemoveDirectory("dir_link"), -1);
    EXPECT_TRUE(std::filesystem::is_directory(Root() / "empty_dir"));
}

TEST_F(PhysicalFileSystemLinkTest, StatSaysWhetherAPathIsALink) {
    std::filesystem::create_directories(Root() / "dir");
    WriteHostFile(Root() / "file.txt", "x");
    std::filesystem::create_symlink(Root() / "dir", Root() / "dir_link");
    auto fs = PhysicalFileSystem::Create(Root().string());

    FileStatus status;
    ASSERT_EQ(fs->Stat("dir_link", status), 0);
    // Everything else is the target's, as stat() says.
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
    EXPECT_TRUE(status.symbolicLink);
    ASSERT_EQ(fs->Stat("dir", status), 0);
    EXPECT_FALSE(status.symbolicLink);
    ASSERT_EQ(fs->Stat("file.txt", status), 0);
    EXPECT_FALSE(status.symbolicLink);
}

TEST_F(PhysicalFileSystemLinkTest, TheRootItselfCanBeNeitherRemovedNorCreated) {
    auto fs = PhysicalFileSystem::Create(Root().string());

    EXPECT_EQ(fs->RemoveDirectory("/"), -1);
    EXPECT_EQ(fs->RemoveDirectory(""), -1);
    EXPECT_EQ(fs->RemoveDirectory("."), -1);
    EXPECT_EQ(fs->RemoveFile("/"), -1);
    EXPECT_EQ(fs->CreateDirectory("/", S_IRWXU), -1);
    EXPECT_TRUE(std::filesystem::is_directory(Root()));
}
#endif
