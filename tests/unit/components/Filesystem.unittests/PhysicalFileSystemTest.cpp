#include <gtest/gtest.h>
#include "PhysicalFileSystem.h"
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
        FileSystem fs;
        ASSERT_EQ(fs.CreateDirectory(kRootDir, S_IRWXU), 0);
    }

    void TearDown() override {
        ::unlink((kRootDir + "/inside.txt").c_str());
        ::rmdir(kRootDir.c_str());
    }
};

TEST_F(PhysicalFileSystemTest, WriteAndReadWithinRootSucceeds) {
    PhysicalFileSystem fs(kRootDir);

    int fd = fs.OpenFile("inside.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_GE(fd, 0);
    const char* data = "hello";
    EXPECT_EQ(fs.WriteFile(fd, data, std::strlen(data)), static_cast<ssize_t>(std::strlen(data)));
    fs.CloseFile(fd);

    fd = fs.OpenFile("inside.txt", O_RDONLY);
    ASSERT_GE(fd, 0);
    char buf[16] = {};
    ssize_t n = fs.ReadFile(fd, buf, sizeof(buf) - 1);
    fs.CloseFile(fd);
    EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), "hello");
}

TEST_F(PhysicalFileSystemTest, TraversalOutsideRootIsRejected) {
    PhysicalFileSystem fs(kRootDir);

    int fd = fs.OpenFile("../outside.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_EQ(fd, -1);
}

TEST_F(PhysicalFileSystemTest, ReadDirectoryListsCreatedFile) {
    PhysicalFileSystem fs(kRootDir);

    int fd = fs.OpenFile("inside.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_GE(fd, 0);
    fs.CloseFile(fd);

    auto entries = fs.ReadDirectory(".");
    bool found = false;
    for (const auto& entry : entries) {
        if (entry.name == "inside.txt") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

} // namespace
