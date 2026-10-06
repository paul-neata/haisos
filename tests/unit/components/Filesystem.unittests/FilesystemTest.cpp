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
    auto file = fs->OpenFile(kTestFile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    EXPECT_NE(file, nullptr);
    file.reset();
    EXPECT_EQ(::access(kTestFile.c_str(), F_OK), 0);
}

TEST_F(FilesystemTest, OpenFileNonExistentReturnsError) {
    auto fs = FileSystem::Create();
    EXPECT_EQ(fs->OpenFile(kTestFile, O_RDONLY), nullptr);
}

TEST_F(FilesystemTest, WriteFileAndReadFile) {
    auto fs = FileSystem::Create();
    fs->CreateDirectory(kTestDir, S_IRWXU);

    // Write
    auto fileW = fs->OpenFile(kTestFile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    ASSERT_NE(fileW, nullptr);
    std::string data = "Hello, Filesystem!";
    EXPECT_EQ(fileW->Write(data.data(), data.size()), static_cast<ssize_t>(data.size()));
    fileW.reset();

    // Read
    auto fileR = fs->OpenFile(kTestFile, O_RDONLY);
    ASSERT_NE(fileR, nullptr);
    std::string buf(data.size(), '\0');
    EXPECT_EQ(fileR->Read(buf.data(), buf.size()), static_cast<ssize_t>(data.size()));
    EXPECT_EQ(buf, data);

    // Read partial
    fileR = fs->OpenFile(kTestFile, O_RDONLY);
    ASSERT_NE(fileR, nullptr);
    std::string small(5, '\0');
    EXPECT_EQ(fileR->Read(small.data(), small.size()), 5);
    EXPECT_EQ(small, "Hello");
}

TEST_F(FilesystemTest, ReadFilePastEnd) {
    auto fs = FileSystem::Create();
    fs->CreateDirectory(kTestDir, S_IRWXU);
    auto file = fs->OpenFile(kTestFile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    ASSERT_NE(file, nullptr);
    std::string data = "abc";
    file->Write(data.data(), data.size());
    file.reset();

    file = fs->OpenFile(kTestFile, O_RDONLY);
    ASSERT_NE(file, nullptr);
    std::string buf(100, '\0');
    EXPECT_EQ(file->Read(buf.data(), buf.size()), 3);
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
    fs->OpenFile(kTestFile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);

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
