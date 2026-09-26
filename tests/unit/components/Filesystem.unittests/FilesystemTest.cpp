#include <gtest/gtest.h>
#include "Filesystem.h"
#include <cstring>

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
#define access _access
#define getcwd _getcwd
#ifndef F_OK
#define F_OK 0
#endif
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
const std::string kTestDir = []() {
    char buf[MAX_PATH];
    DWORD len = GetTempPathA(MAX_PATH, buf);
    return std::string(buf, len) + "haisos_fs_test";
}();
#else
const std::string kTestDir = "/tmp/haisos_fs_test";
#endif
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
    auto fs = FileSystem::Create();
    fs->CreateDirectory(kTestDir, S_IRWXU);
    int fd = fs->OpenFile(kTestFile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    EXPECT_GE(fd, 0);
    EXPECT_EQ(fs->CloseFile(fd), 0);
    EXPECT_EQ(::access(kTestFile.c_str(), F_OK), 0);
}

TEST_F(FilesystemTest, OpenFileNonExistentReturnsError) {
    auto fs = FileSystem::Create();
    int fd = fs->OpenFile(kTestFile, O_RDONLY);
    EXPECT_LT(fd, 0);
}

TEST_F(FilesystemTest, CloseFileInvalidFdReturnsError) {
    auto fs = FileSystem::Create();
    EXPECT_LT(fs->CloseFile(-1), 0);
}

TEST_F(FilesystemTest, WriteFileAndReadFile) {
    auto fs = FileSystem::Create();
    fs->CreateDirectory(kTestDir, S_IRWXU);

    // Write
    int fdw = fs->OpenFile(kTestFile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    ASSERT_GE(fdw, 0);
    std::string data = "Hello, Filesystem!";
    EXPECT_EQ(fs->WriteFile(fdw, data.data(), data.size()), static_cast<ssize_t>(data.size()));
    EXPECT_EQ(fs->CloseFile(fdw), 0);

    // Read
    int fdr = fs->OpenFile(kTestFile, O_RDONLY);
    ASSERT_GE(fdr, 0);
    std::string buf(data.size(), '\0');
    EXPECT_EQ(fs->ReadFile(fdr, buf.data(), buf.size()), static_cast<ssize_t>(data.size()));
    EXPECT_EQ(buf, data);

    // Read partial
    EXPECT_EQ(fs->CloseFile(fdr), 0);
    fdr = fs->OpenFile(kTestFile, O_RDONLY);
    ASSERT_GE(fdr, 0);
    std::string small(5, '\0');
    EXPECT_EQ(fs->ReadFile(fdr, small.data(), small.size()), 5);
    EXPECT_EQ(small, "Hello");
    fs->CloseFile(fdr);
}

TEST_F(FilesystemTest, ReadFilePastEnd) {
    auto fs = FileSystem::Create();
    fs->CreateDirectory(kTestDir, S_IRWXU);
    int fd = fs->OpenFile(kTestFile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    ASSERT_GE(fd, 0);
    std::string data = "abc";
    fs->WriteFile(fd, data.data(), data.size());
    fs->CloseFile(fd);

    fd = fs->OpenFile(kTestFile, O_RDONLY);
    ASSERT_GE(fd, 0);
    std::string buf(100, '\0');
    EXPECT_EQ(fs->ReadFile(fd, buf.data(), buf.size()), 3);
    fs->CloseFile(fd);
}

TEST_F(FilesystemTest, ReadFileReturnsErrorOnInvalidFd) {
    auto fs = FileSystem::Create();
    char buf[4] = {};
    EXPECT_LT(fs->ReadFile(-1, buf, sizeof(buf)), 0);
}

// A descriptor that is not open is refused like any other bad one. The
// Microsoft C runtime would end the whole program over it, were its invalid
// parameter handler not told otherwise (see CrtInvalidParameterAsError).
TEST_F(FilesystemTest, ADescriptorThatIsNotOpenIsRefused) {
    auto fs = FileSystem::Create();
    const int notOpen = 9999;
    char buf[4] = {};
    EXPECT_LT(fs->ReadFile(notOpen, buf, sizeof(buf)), 0);
    EXPECT_LT(fs->WriteFile(notOpen, buf, sizeof(buf)), 0);
    EXPECT_LT(fs->CloseFile(notOpen), 0);
}

TEST_F(FilesystemTest, WriteFileReturnsErrorOnInvalidFd) {
    auto fs = FileSystem::Create();
    std::string data = "test";
    EXPECT_LT(fs->WriteFile(-1, data.data(), data.size()), 0);
}

TEST_F(FilesystemTest, CreateDirectoryAndRemoveDirectory) {
    auto fs = FileSystem::Create();

    EXPECT_EQ(fs->CreateDirectory(kTestDir, S_IRWXU), 0);
    EXPECT_EQ(::access(kTestDir.c_str(), F_OK), 0);

    EXPECT_EQ(fs->RemoveDirectory(kTestDir), 0);
    EXPECT_LT(::access(kTestDir.c_str(), F_OK), 0);
}

TEST_F(FilesystemTest, CreateDirectoryExistingReturnsError) {
    auto fs = FileSystem::Create();
    fs->CreateDirectory(kTestDir, S_IRWXU);
    EXPECT_LT(fs->CreateDirectory(kTestDir, S_IRWXU), 0);
    fs->RemoveDirectory(kTestDir);
}

TEST_F(FilesystemTest, RemoveDirectoryNonExistentReturnsError) {
    auto fs = FileSystem::Create();
    EXPECT_LT(fs->RemoveDirectory("/tmp/haisos_nonexistent_dir_12345"), 0);
}

TEST_F(FilesystemTest, ReadDirectoryListsEntries) {
    auto fs = FileSystem::Create();

    fs->CreateDirectory(kTestDir, S_IRWXU);
    int fd = fs->OpenFile(kTestFile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    fs->CloseFile(fd);

    auto entries = fs->ReadDirectory(kTestDir);

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
    fs->RemoveDirectory(kTestDir); // will fail because file exists, that's fine for test
}

TEST_F(FilesystemTest, ReadDirectoryListsDotAndDotDotFirstOnce) {
    auto fs = FileSystem::Create();
    fs->CreateDirectory(kTestDir, S_IRWXU);

    auto entries = fs->ReadDirectory(kTestDir);
    ASSERT_GE(entries.size(), 2u);
    EXPECT_EQ(entries[0].name, ".");
    EXPECT_EQ(entries[0].type, DirectoryEntryType::Dir);
    EXPECT_EQ(entries[1].name, "..");
    EXPECT_EQ(entries[1].type, DirectoryEntryType::Dir);
    for (size_t i = 2; i < entries.size(); ++i) {
        EXPECT_NE(entries[i].name, ".");
        EXPECT_NE(entries[i].name, "..");
    }

    fs->RemoveDirectory(kTestDir);
}

TEST_F(FilesystemTest, ReadDirectoryNonExistentReturnsEmpty) {
    auto fs = FileSystem::Create();
    auto entries = fs->ReadDirectory("/tmp/haisos_nonexistent_dir_12345");
    EXPECT_TRUE(entries.empty());
}

} // namespace
