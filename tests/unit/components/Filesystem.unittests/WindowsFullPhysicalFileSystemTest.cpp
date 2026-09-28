// WindowsFullPhysicalFileSystem exists on Windows alone: on Linux the full
// physical filesystem is a PhysicalFileSystem at "/" (see FactoryTest).
#ifdef _WIN32
#include <gtest/gtest.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include "PhysicalFileSystem.h"
#include "FilesystemUtils.h"
#include "windows/WindowsFullPhysicalFileSystem.h"
#include <windows.h>
#undef CreateDirectory
#undef RemoveDirectory
#undef GetCurrentDirectory

using namespace Haisos;

namespace {

// A directory of its own under the system's temporary directory, and the same
// directory as a path of the full physical filesystem: /c/Users/...
class WindowsFullPhysicalFileSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test = ::testing::UnitTest::GetInstance()->current_test_info();
        m_dir = std::filesystem::temp_directory_path() / (std::string("haisos_full_fs_") + test->name());
        std::filesystem::remove_all(m_dir);
        std::filesystem::create_directories(m_dir);
        std::ofstream(m_dir / "f.txt") << "on the disk";
        ASSERT_EQ(m_dir.root_name().u8string().size(), 2u) << "the temporary directory is on no drive";
        m_drive = static_cast<char>(std::tolower(static_cast<unsigned char>(m_dir.root_name().u8string()[0])));
        m_full = "/" + std::string(1, m_drive) + "/" + m_dir.relative_path().generic_u8string();
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(m_dir, ec);
    }

    static std::string ReadAll(IFileSystem& fs, const std::string& path) {
        std::string content;
        EXPECT_TRUE(ReadWholeFile(fs, path, content)) << path;
        return content;
    }

    static std::vector<std::string> Names(IFileSystem& fs, const std::string& path) {
        std::vector<std::string> names;
        for (const auto& entry : fs.ReadDirectory(path)) {
            names.push_back(entry.name);
        }
        return names;
    }

    std::shared_ptr<WindowsFullPhysicalFileSystem> m_fs = WindowsFullPhysicalFileSystem::Create();
    std::filesystem::path m_dir;
    char m_drive = 'c';
    std::string m_full;
};

// A letter no drive has, if there is one.
char UnusedDriveLetter() {
    const DWORD drives = ::GetLogicalDrives();
    for (char letter = 'z'; letter >= 'a'; --letter) {
        if ((drives & (1u << (letter - 'a'))) == 0) {
            return letter;
        }
    }
    return '\0';
}

} // namespace

TEST_F(WindowsFullPhysicalFileSystemTest, TheRootListsEveryDriveByItsLowercaseLetter) {
    const auto entries = m_fs->ReadDirectory("/");
    ASSERT_GE(entries.size(), 3u);
    EXPECT_EQ(entries[0].name, ".");
    EXPECT_EQ(entries[1].name, "..");
    std::string letters;
    for (size_t i = 2; i < entries.size(); ++i) {
        ASSERT_EQ(entries[i].name.size(), 1u) << entries[i].name;
        EXPECT_TRUE(entries[i].name[0] >= 'a' && entries[i].name[0] <= 'z') << entries[i].name;
        EXPECT_EQ(entries[i].type, DirectoryEntryType::Dir) << entries[i].name;
        letters += entries[i].name;
    }
    // Every drive GetLogicalDrives knows, and no other.
    std::string expected;
    const DWORD drives = ::GetLogicalDrives();
    for (char letter = 'a'; letter <= 'z'; ++letter) {
        if (drives & (1u << (letter - 'a'))) {
            expected += letter;
        }
    }
    EXPECT_EQ(letters, expected);
    EXPECT_NE(letters.find(m_drive), std::string::npos);
}

TEST_F(WindowsFullPhysicalFileSystemTest, APathBelowADriveIsThatPathOnTheDrive) {
    EXPECT_EQ(ReadAll(*m_fs, m_full + "/f.txt"), "on the disk");
    // The letter in either case, and either separator.
    std::string upper = m_full;
    upper[1] = static_cast<char>(std::toupper(static_cast<unsigned char>(upper[1])));
    EXPECT_EQ(ReadAll(*m_fs, upper + "/f.txt"), "on the disk");
    std::string backward = m_full;
    std::replace(backward.begin(), backward.end(), '/', '\\');
    EXPECT_EQ(ReadAll(*m_fs, backward + "\\f.txt"), "on the disk");

    const auto names = Names(*m_fs, m_full);
    EXPECT_NE(std::find(names.begin(), names.end(), "f.txt"), names.end());
    // The drive itself lists as the drive's root does.
    const auto top = Names(*m_fs, "/" + std::string(1, m_drive));
    const std::string first = m_dir.relative_path().begin()->u8string();
    EXPECT_NE(std::find(top.begin(), top.end(), first), top.end()) << first;
}

TEST_F(WindowsFullPhysicalFileSystemTest, WritingBelowADriveWritesTheDisk) {
    ASSERT_EQ(m_fs->CreateDirectory(m_full + "/made", _S_IREAD | _S_IWRITE), 0);
    const int fd = m_fs->OpenFile(m_full + "/made/new.txt", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(m_fs->WriteFile(fd, "written", 7), 7);
    EXPECT_EQ(m_fs->CloseFile(fd), 0);
    std::stringstream content;
    {
        // Closed before the removal below: Windows removes no open file.
        std::ifstream in(m_dir / "made" / "new.txt", std::ios::binary);
        content << in.rdbuf();
    }
    EXPECT_EQ(content.str(), "written");

    EXPECT_EQ(m_fs->RemoveFile(m_full + "/made/new.txt"), 0);
    EXPECT_EQ(m_fs->RemoveDirectory(m_full + "/made"), 0);
    EXPECT_FALSE(std::filesystem::exists(m_dir / "made"));
}

TEST_F(WindowsFullPhysicalFileSystemTest, TheRootAndADriveAreDirectories) {
    FileStatus status;
    ASSERT_EQ(m_fs->Stat("/", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
    EXPECT_FALSE(status.symbolicLink);
    ASSERT_EQ(m_fs->Stat("/" + std::string(1, m_drive), status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
    EXPECT_FALSE(status.symbolicLink);
    ASSERT_EQ(m_fs->Stat(m_full + "/f.txt", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::File);
    EXPECT_EQ(status.size, 11u);
}

// No drive is made or removed at the top, nor is a drive's root.
TEST_F(WindowsFullPhysicalFileSystemTest, NothingIsCreatedOrRemovedAtTheTop) {
    const std::string drive = "/" + std::string(1, m_drive);
    EXPECT_NE(m_fs->CreateDirectory("/newdir", _S_IREAD | _S_IWRITE), 0);
    EXPECT_NE(m_fs->CreateDirectory(drive, _S_IREAD | _S_IWRITE), 0);
    EXPECT_NE(m_fs->RemoveDirectory(drive), 0);
    EXPECT_NE(m_fs->RemoveFile(drive), 0);
    EXPECT_NE(m_fs->RemoveDirectory("/"), 0);
    EXPECT_LT(m_fs->OpenFile("/new.txt", kFileOpenWriteCreateTruncate, kFileCreateMode), 0);
    EXPECT_LT(m_fs->OpenFile("/", kFileOpenReadOnly), 0);
    FileStatus status;
    EXPECT_NE(m_fs->Stat("/newdir", status), 0);
}

TEST_F(WindowsFullPhysicalFileSystemTest, ADriveThatIsNotThereHoldsNothing) {
    const char unused = UnusedDriveLetter();
    if (unused == '\0') {
        GTEST_SKIP() << "every drive letter is taken";
    }
    const std::string drive = "/" + std::string(1, unused);
    FileStatus status;
    EXPECT_NE(m_fs->Stat(drive, status), 0);
    EXPECT_TRUE(m_fs->ReadDirectory(drive).empty());
    EXPECT_LT(m_fs->OpenFile(drive + "/x.txt", kFileOpenWriteCreateTruncate, kFileCreateMode), 0);
    EXPECT_NE(m_fs->CreateDirectory(drive + "/x", _S_IREAD | _S_IWRITE), 0);
}

// A drive of removable or optical media with no medium in lists as empty, and
// nothing waits on a "no disk" dialog.
TEST_F(WindowsFullPhysicalFileSystemTest, ADriveWithNoMediumInListsAsEmpty) {
    std::string empty;
    const DWORD drives = ::GetLogicalDrives();
    for (char letter = 'a'; letter <= 'z'; ++letter) {
        if ((drives & (1u << (letter - 'a'))) == 0) {
            continue;
        }
        const wchar_t root[] = {static_cast<wchar_t>(letter - 'a' + 'A'), L':', L'\\', L'\0'};
        const UINT type = ::GetDriveTypeW(root);
        const UINT previous = ::SetErrorMode(SEM_FAILCRITICALERRORS);
        const bool noMedium = (type == DRIVE_REMOVABLE || type == DRIVE_CDROM) &&
            !::GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0);
        ::SetErrorMode(previous);
        if (noMedium) {
            empty += letter;
        }
    }
    if (empty.empty()) {
        GTEST_SKIP() << "no removable or optical drive without a medium";
    }
    for (char letter : empty) {
        const std::string drive = "/" + std::string(1, letter);
        EXPECT_EQ(Names(*m_fs, drive), (std::vector<std::string>{".", ".."})) << drive;
        FileStatus status;
        ASSERT_EQ(m_fs->Stat(drive, status), 0) << drive;
        EXPECT_EQ(status.type, DirectoryEntryType::Dir);
        EXPECT_LT(m_fs->OpenFile(drive + "/x.txt", kFileOpenReadOnly), 0);
    }
}

// A physical filesystem rooted at the full filesystem's root is the full
// filesystem: "/" holds the drives on Windows.
TEST_F(WindowsFullPhysicalFileSystemTest, APhysicalFileSystemAtTheTopIsTheFullOne) {
    for (const char* top : {"/", "\\", "/."}) {
        auto fs = PhysicalFileSystem::Create(top);
        const auto names = Names(*fs, "/");
        EXPECT_NE(std::find(names.begin(), names.end(), std::string(1, m_drive)), names.end()) << top;
        EXPECT_EQ(ReadAll(*fs, m_full + "/f.txt"), "on the disk") << top;
    }
}

#endif // _WIN32
