#include <gtest/gtest.h>
#include "Filesystem.h"
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>

using namespace Haisos;

namespace {

const std::string kTestDir = "/tmp/haisos_fs_test";
const std::string kTestFile = kTestDir + "/testfile.txt";

class FilesystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up from previous runs
        ::unlink(kTestFile.c_str());
        ::rmdir(kTestDir.c_str());
    }

    void TearDown() override {
        ::unlink(kTestFile.c_str());
        ::rmdir(kTestDir.c_str());
    }
};

TEST_F(FilesystemTest, OpenFileCreatesNewFile) {
    Filesystem fs;
    fs.CreateDirectory(kTestDir, S_IRWXU);
    int fd = fs.OpenFile(kTestFile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    EXPECT_GE(fd, 0);
    EXPECT_EQ(fs.CloseFile(fd), 0);
    EXPECT_EQ(::access(kTestFile.c_str(), F_OK), 0);
}

TEST_F(FilesystemTest, OpenFileNonExistentReturnsError) {
    Filesystem fs;
    int fd = fs.OpenFile(kTestFile, O_RDONLY);
    EXPECT_LT(fd, 0);
}

TEST_F(FilesystemTest, CloseFileInvalidFdReturnsError) {
    Filesystem fs;
    EXPECT_LT(fs.CloseFile(-1), 0);
}

TEST_F(FilesystemTest, WriteFileAndReadFile) {
    Filesystem fs;
    fs.CreateDirectory(kTestDir, S_IRWXU);

    // Write
    int fdw = fs.OpenFile(kTestFile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    ASSERT_GE(fdw, 0);
    std::string data = "Hello, Filesystem!";
    EXPECT_EQ(fs.WriteFile(fdw, data.data(), data.size()), static_cast<int>(data.size()));
    EXPECT_EQ(fs.CloseFile(fdw), 0);

    // Read
    int fdr = fs.OpenFile(kTestFile, O_RDONLY);
    ASSERT_GE(fdr, 0);
    std::string buf(data.size(), '\0');
    EXPECT_EQ(fs.ReadFile(fdr, buf.data(), buf.size()), static_cast<int>(data.size()));
    EXPECT_EQ(buf, data);

    // Read partial
    EXPECT_EQ(fs.CloseFile(fdr), 0);
    fdr = fs.OpenFile(kTestFile, O_RDONLY);
    ASSERT_GE(fdr, 0);
    std::string small(5, '\0');
    EXPECT_EQ(fs.ReadFile(fdr, small.data(), small.size()), 5);
    EXPECT_EQ(small, "Hello");
    fs.CloseFile(fdr);
}

TEST_F(FilesystemTest, ReadFilePastEnd) {
    Filesystem fs;
    fs.CreateDirectory(kTestDir, S_IRWXU);
    int fd = fs.OpenFile(kTestFile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    ASSERT_GE(fd, 0);
    std::string data = "abc";
    fs.WriteFile(fd, data.data(), data.size());
    fs.CloseFile(fd);

    fd = fs.OpenFile(kTestFile, O_RDONLY);
    ASSERT_GE(fd, 0);
    std::string buf(100, '\0');
    EXPECT_EQ(fs.ReadFile(fd, buf.data(), buf.size()), 3);
    fs.CloseFile(fd);
}

TEST_F(FilesystemTest, WriteFileReturnsErrorOnInvalidFd) {
    Filesystem fs;
    std::string data = "test";
    EXPECT_LT(fs.WriteFile(-1, data.data(), data.size()), 0);
}

TEST_F(FilesystemTest, CreateDirectoryAndRemoveDirectory) {
    Filesystem fs;

    EXPECT_EQ(fs.CreateDirectory(kTestDir, S_IRWXU), 0);
    EXPECT_EQ(::access(kTestDir.c_str(), F_OK), 0);

    EXPECT_EQ(fs.RemoveDirectory(kTestDir), 0);
    EXPECT_LT(::access(kTestDir.c_str(), F_OK), 0);
}

TEST_F(FilesystemTest, CreateDirectoryExistingReturnsError) {
    Filesystem fs;
    fs.CreateDirectory(kTestDir, S_IRWXU);
    EXPECT_LT(fs.CreateDirectory(kTestDir, S_IRWXU), 0);
    fs.RemoveDirectory(kTestDir);
}

TEST_F(FilesystemTest, RemoveDirectoryNonExistentReturnsError) {
    Filesystem fs;
    EXPECT_LT(fs.RemoveDirectory("/tmp/haisos_nonexistent_dir_12345"), 0);
}

TEST_F(FilesystemTest, ChangeDirectoryAndGetCurrentDirectory) {
    Filesystem fs;

    // Save original
    std::string original;
    original.resize(4096);
    char* originalCwd = ::getcwd(original.data(), original.size());
    ASSERT_NE(originalCwd, nullptr);
    original.resize(std::strlen(originalCwd));

    // Create and change
    fs.CreateDirectory(kTestDir, S_IRWXU);
    EXPECT_EQ(fs.ChangeDirectory(kTestDir), 0);

    std::string cwd;
    EXPECT_NE(fs.GetCurrentDirectory(cwd, 4096), nullptr);
    EXPECT_EQ(cwd, kTestDir);

    // Restore
    fs.ChangeDirectory(original);
    fs.RemoveDirectory(kTestDir);
}

TEST_F(FilesystemTest, GetCurrentDirectoryNullOnZeroSize) {
    Filesystem fs;
    std::string buf;
    EXPECT_EQ(fs.GetCurrentDirectory(buf, 0), nullptr);
}

TEST_F(FilesystemTest, ReadDirectoryListsEntries) {
    Filesystem fs;

    fs.CreateDirectory(kTestDir, S_IRWXU);
    int fd = fs.OpenFile(kTestFile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    fs.CloseFile(fd);

    auto entries = fs.ReadDirectory(kTestDir);

    bool foundFile = false;
    bool foundDir = false;
    for (const auto& e : entries) {
        if (e.name == "testfile.txt") {
            EXPECT_EQ(e.type, DirectoryEntryType::File);
            foundFile = true;
        }
    }
    EXPECT_TRUE(foundFile);

    // Cleanup
    fs.RemoveDirectory(kTestDir); // will fail because file exists, that's fine for test
}

TEST_F(FilesystemTest, ReadDirectoryFiltersDotAndDotDot) {
    Filesystem fs;
    fs.CreateDirectory(kTestDir, S_IRWXU);

    auto entries = fs.ReadDirectory(kTestDir);
    for (const auto& e : entries) {
        EXPECT_NE(e.name, ".");
        EXPECT_NE(e.name, "..");
    }

    fs.RemoveDirectory(kTestDir);
}

TEST_F(FilesystemTest, ReadDirectoryNonExistentReturnsEmpty) {
    Filesystem fs;
    auto entries = fs.ReadDirectory("/tmp/haisos_nonexistent_dir_12345");
    EXPECT_TRUE(entries.empty());
}

} // namespace
