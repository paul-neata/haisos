#include <gtest/gtest.h>
#include "PhysicalFileSystem.h"
#include "Filesystem.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

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

// --- Names, and the forms a root may be written in ---

namespace {

// A root of its own under the system's temporary directory, removed after
// each test.
class PhysicalFileSystemNameTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path() / (std::string("haisos_pfs_names_") + test->name());
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directories(m_root / "sub");
        std::ofstream(m_root / "sub" / "f.txt") << "in sub";
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    std::string Root() const { return m_root.u8string(); }

    static bool Opens(IFileSystem& fs, const std::string& path) {
        const int fd = fs.OpenFile(path, O_RDONLY);
        if (fd < 0) {
            return false;
        }
        fs.CloseFile(fd);
        return true;
    }

    static bool Creates(IFileSystem& fs, const std::string& path) {
        const int fd = fs.OpenFile(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (fd < 0) {
            return false;
        }
        fs.CloseFile(fd);
        return true;
    }

    std::filesystem::path m_root;
};

} // namespace

// A NUL would end the name early on the host: "a\0b" would be "a".
TEST_F(PhysicalFileSystemNameTest, ANameHoldingANulIsRefused) {
    auto fs = PhysicalFileSystem::Create(Root());
    EXPECT_FALSE(Creates(*fs, std::string("/a\0b", 4)));
    EXPECT_FALSE(std::filesystem::exists(m_root / "a"));
}

// A path is split where the mounts over the filesystem split it -- at '\' too
// on Windows, where the host takes it as a separator; elsewhere '\' is part of
// a name, as it is to the host.
TEST_F(PhysicalFileSystemNameTest, ABackslashSeparatesOnlyOnWindows) {
    auto fs = PhysicalFileSystem::Create(Root());
#ifdef _WIN32
    EXPECT_TRUE(Opens(*fs, "sub\\f.txt"));
    EXPECT_TRUE(Opens(*fs, "\\sub\\f.txt"));
    // So "..\" climbs as "../" does, and above the root is refused alike.
    EXPECT_TRUE(Opens(*fs, "sub\\..\\sub\\f.txt"));
    EXPECT_FALSE(Creates(*fs, "..\\outside.txt"));
    EXPECT_FALSE(std::filesystem::exists(m_root.parent_path() / "outside.txt"));
#else
    EXPECT_FALSE(Opens(*fs, "sub\\f.txt"));
    ASSERT_TRUE(Creates(*fs, "a\\b"));
    EXPECT_TRUE(std::filesystem::exists(m_root / "a\\b"));
#endif
}

// Names are UTF-8, as everywhere in Haisos; on Windows they reach the host as
// UTF-16, not through the ANSI code page, which would name another file.
TEST_F(PhysicalFileSystemNameTest, NamesAreUtf8) {
    auto fs = PhysicalFileSystem::Create(Root());
    const std::string name = "caf\xC3\xA9 \xE2\x82\xAC \xE6\x97\xA5.txt";
    ASSERT_TRUE(Creates(*fs, "/sub/" + name));
    EXPECT_TRUE(std::filesystem::exists(m_root / "sub" / std::filesystem::u8path(name)));
    bool listed = false;
    for (const auto& entry : fs->ReadDirectory("/sub")) {
        listed = listed || entry.name == name;
    }
    EXPECT_TRUE(listed);
    FileStatus status;
    EXPECT_EQ(fs->Stat("/sub/" + name, status), 0);
    EXPECT_EQ(fs->RemoveFile("/sub/" + name), 0);
    EXPECT_FALSE(std::filesystem::exists(m_root / "sub" / std::filesystem::u8path(name)));
}

// A root naming no directory at all gives a filesystem on which every call
// fails, rather than one rooted somewhere else.
TEST_F(PhysicalFileSystemNameTest, ARootNamingNoDirectoryFailsEveryCall) {
    std::vector<std::string> roots = {""};
#ifdef _WIN32
    roots.push_back("/tmp/haisos_nowhere");  // the full filesystem has no /tmp
    roots.push_back("c:relative");           // relative to drive C:'s own current directory
#endif
    for (const auto& root : roots) {
        auto fs = PhysicalFileSystem::Create(root);
        FileStatus status;
        EXPECT_NE(fs->Stat("/", status), 0) << root;
        EXPECT_FALSE(Creates(*fs, "/x.txt")) << root;
    }
}

#ifdef _WIN32
namespace {

// |path| as a path of the full physical filesystem: C:\x -> /c/x.
std::string FullPathOf(const std::filesystem::path& path) {
    const std::string drive = path.root_name().u8string();
    return "/" + std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(drive[0])))) +
        "/" + path.relative_path().generic_u8string();
}

} // namespace

// On Windows a root may be written with its drive letter or as a path of the
// full physical filesystem, with '\' or '/' in any mix.
TEST_F(PhysicalFileSystemNameTest, ARootMayBeWrittenInEveryWindowsForm) {
    ASSERT_EQ(m_root.root_name().u8string().size(), 2u) << "the temporary directory is on no drive";
    const std::string full = FullPathOf(m_root);
    std::string backwardFull = full;
    std::replace(backwardFull.begin(), backwardFull.end(), '/', '\\');
    std::string upperFull = full;
    upperFull[1] = static_cast<char>(std::toupper(static_cast<unsigned char>(upperFull[1])));
    for (const std::string& root : {Root(), m_root.generic_u8string(), full, backwardFull, upperFull, full + "/sub/.."}) {
        auto fs = PhysicalFileSystem::Create(root);
        EXPECT_TRUE(Opens(*fs, "/sub/f.txt")) << root;
    }
}

// Names Windows would take for something else are refused, whatever is asked
// of them: a device (CON is the console), a drive or an alternate data
// stream, a name Windows trims ("sub." would be "sub", past any mount at
// "/sub"), or a wildcard.
TEST_F(PhysicalFileSystemNameTest, NamesWindowsTakesForSomethingElseAreRefused) {
    auto fs = PhysicalFileSystem::Create(Root());
    for (const char* name : {"/con", "/nul", "/sub/aux", "/com1", "/lpt9", "/conout$",
                             "/c:x", "/sub/f.txt:hidden", "/sub.", "/sub./f.txt", "/sub /f.txt", "/a?", "/a*"}) {
        EXPECT_FALSE(Opens(*fs, name)) << name;
        EXPECT_FALSE(Creates(*fs, name)) << name;
        EXPECT_NE(fs->CreateDirectory(name, S_IRWXU), 0) << name;
        FileStatus status;
        EXPECT_NE(fs->Stat(name, status), 0) << name;
    }
    // Nothing got created on the way, and "sub" was never reached as "sub.".
    std::vector<std::string> left;
    for (const auto& entry : std::filesystem::directory_iterator(m_root)) {
        left.push_back(entry.path().filename().u8string());
    }
    EXPECT_EQ(left, std::vector<std::string>{"sub"});
    EXPECT_FALSE(std::filesystem::exists(m_root / "sub" / "f.txt:hidden"));
}

// NUL is the null device on every Windows, wherever the path leads; NUL.txt
// is one on Windows 10 and a file on Windows 11 -- as the host says, so a
// file is a file wherever the host takes it for one.
TEST_F(PhysicalFileSystemNameTest, TheHostSaysWhichNamesAreDevices) {
    EXPECT_TRUE(FileSystem::IsDevicePath((m_root / "nul").u8string()));
    EXPECT_FALSE(FileSystem::IsDevicePath((m_root / "sub" / "f.txt").u8string()));

    auto fs = PhysicalFileSystem::Create(Root());
    for (const char* name : {"nul.txt", "aux.c", "con.h", "com1.log"}) {
        const bool device = FileSystem::IsDevicePath((m_root / name).u8string());
        EXPECT_EQ(Creates(*fs, std::string("/") + name), !device) << name;
        EXPECT_EQ(std::filesystem::exists(m_root / name), !device) << name;
    }
}

// A junction is a link: Stat says so, removing it removes the junction only,
// and one leading out of the root is not followed. (mklink /J needs no
// privilege, unlike a symbolic link.)
TEST_F(PhysicalFileSystemNameTest, AJunctionIsALink) {
    std::filesystem::create_directories(m_root / "outside");
    std::ofstream(m_root / "outside" / "secret.txt") << "secret";
    std::filesystem::create_directories(m_root / "jail");
    const std::string command = "mklink /J \"" + (m_root / "jail" / "out").string() + "\" \"" +
        (m_root / "outside").string() + "\" > NUL 2>&1";
    if (std::system(command.c_str()) != 0) {
        GTEST_SKIP() << "could not make a junction: " << command;
    }

    auto fs = PhysicalFileSystem::Create((m_root / "jail").u8string());
    EXPECT_FALSE(Opens(*fs, "/out/secret.txt"));

    auto whole = PhysicalFileSystem::Create(Root());
    FileStatus status;
    ASSERT_EQ(whole->Stat("/jail/out", status), 0);
    EXPECT_TRUE(status.symbolicLink);
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
    ASSERT_EQ(whole->Stat("/outside", status), 0);
    EXPECT_FALSE(status.symbolicLink);

    EXPECT_EQ(whole->RemoveDirectory("/jail/out"), 0);
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(m_root / "jail" / "out")));
    EXPECT_TRUE(std::filesystem::exists(m_root / "outside" / "secret.txt"));
}
#endif
