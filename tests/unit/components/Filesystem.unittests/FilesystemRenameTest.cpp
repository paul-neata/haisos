#include <gtest/gtest.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef CreateDirectory
#undef RemoveDirectory
#undef GetCurrentDirectory
#include "src/components/libheaders/WideText.h"
#endif
#include "src/components/Filesystem/ComposedFileSystem.h"
#include "src/components/Filesystem/DeviceFileSystem.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Filesystem/InMemoryFileSystem.h"
#include "src/components/Filesystem/PhysicalFileSystem.h"
#include "src/components/Filesystem/ReadOnlyFileSystem.h"
#include "src/components/Filesystem/SubFileSystem.h"

using namespace Haisos;

namespace {

bool WriteFile(IFileSystem& fs, const std::string& path, const std::string& contents) {
    // With a mode: O_CREAT without one is refused by libc on the host.
    auto file = fs.OpenFile(path, kFileOpenWriteCreateTruncate, kFileCreateMode);
    if (!file) {
        return false;
    }
    return file->Write(contents.data(), contents.size()) == static_cast<ssize_t>(contents.size());
}

std::shared_ptr<InMemoryFileSystem> MakeInMemoryWith(const std::string& path, const std::string& contents) {
    auto fs = InMemoryFileSystem::Create();
    EXPECT_TRUE(WriteFile(*fs, path, contents));
    return fs;
}

std::string ReadAll(IFileSystem& fs, const std::string& path) {
    std::string content;
    return ReadWholeFile(fs, path, content) ? content : std::string("<unreadable>");
}

// The whole of a file on the real disk, or "" if there is none.
std::string ReadHostFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Lists(const std::vector<DirectoryEntry>& entries, const std::string& name) {
    for (const auto& entry : entries) {
        if (entry.name == name) {
            return true;
        }
    }
    return false;
}

} // namespace

// --- Rename, on the in-memory filesystem ---

TEST(FilesystemRenameTest, RenamesAFileAndItsContentFollows) {
    auto fs = MakeInMemoryWith("/a.txt", "x");
    EXPECT_EQ(fs->Rename("/a.txt", "/b.txt"), 0);
    EXPECT_EQ(ReadAll(*fs, "/b.txt"), "x");
    FileStatus status;
    EXPECT_EQ(fs->Stat("/a.txt", status), kFileSystemError);
}

TEST(FilesystemRenameTest, ReplacesAnExistingFile) {
    auto fs = MakeInMemoryWith("/a", "1");
    ASSERT_TRUE(WriteFile(*fs, "/b", "2"));
    EXPECT_EQ(fs->Rename("/a", "/b"), 0);
    EXPECT_EQ(ReadAll(*fs, "/b"), "1");
}

TEST(FilesystemRenameTest, RenamesADirectoryWithEverythingInIt) {
    auto fs = InMemoryFileSystem::Create();
    ASSERT_EQ(fs->CreateDirectory("/d", 0), 0);
    ASSERT_EQ(fs->CreateDirectory("/d/e", 0), 0);
    ASSERT_TRUE(WriteFile(*fs, "/d/e/f.txt", "deep"));

    EXPECT_EQ(fs->Rename("/d", "/g"), 0);
    EXPECT_EQ(ReadAll(*fs, "/g/e/f.txt"), "deep");

    const auto root = fs->ReadDirectory("/");
    EXPECT_TRUE(Lists(root, "g"));
    EXPECT_FALSE(Lists(root, "d"));
}

TEST(FilesystemRenameTest, ReplacesOnlyAnEmptyDirectory) {
    auto fs = InMemoryFileSystem::Create();
    ASSERT_EQ(fs->CreateDirectory("/d1", 0), 0);
    ASSERT_EQ(fs->CreateDirectory("/d2", 0), 0);
    ASSERT_EQ(fs->CreateDirectory("/d3", 0), 0);
    ASSERT_TRUE(WriteFile(*fs, "/d3/inside.txt", "x"));

    // An empty directory is replaced by a directory, as rename() does.
    EXPECT_EQ(fs->Rename("/d1", "/d2"), 0);
    FileStatus status;
    EXPECT_EQ(fs->Stat("/d2", status), 0);
    EXPECT_EQ(fs->Stat("/d1", status), kFileSystemError);

    // A directory holding anything is not.
    ASSERT_EQ(fs->CreateDirectory("/d4", 0), 0);
    EXPECT_EQ(fs->Rename("/d4", "/d3"), kFileSystemError);
    EXPECT_EQ(fs->Stat("/d4", status), 0); // both unchanged
    EXPECT_EQ(fs->Stat("/d3/inside.txt", status), 0);
}

TEST(FilesystemRenameTest, RefusesTypeMismatches) {
    auto fs = MakeInMemoryWith("/f", "x");
    ASSERT_EQ(fs->CreateDirectory("/d", 0), 0);
    ASSERT_EQ(fs->CreateDirectory("/e", 0), 0);

    // A file does not replace a directory, nor a directory a file.
    EXPECT_EQ(fs->Rename("/f", "/d"), kFileSystemError);
    EXPECT_EQ(fs->Rename("/e", "/f"), kFileSystemError);
    FileStatus status;
    EXPECT_EQ(fs->Stat("/f", status), 0);
    EXPECT_EQ(fs->Stat("/d", status), 0);
}

TEST(FilesystemRenameTest, RefusesMovingADirectoryIntoItself) {
    auto fs = InMemoryFileSystem::Create();
    ASSERT_EQ(fs->CreateDirectory("/d", 0), 0);
    EXPECT_EQ(fs->Rename("/d", "/d/sub"), kFileSystemError);
}

TEST(FilesystemRenameTest, FailsForAMissingSourceOrParent) {
    auto fs = InMemoryFileSystem::Create();
    EXPECT_EQ(fs->Rename("/nope", "/x"), kFileSystemError);
    ASSERT_TRUE(WriteFile(*fs, "/a", "x"));
    EXPECT_EQ(fs->Rename("/a", "/nodir/x"), kFileSystemError);
    FileStatus status;
    EXPECT_EQ(fs->Stat("/a", status), 0); // nothing was moved
}

TEST(FilesystemRenameTest, RenamingToItselfSucceeds) {
    auto fs = MakeInMemoryWith("/a", "z");
    EXPECT_EQ(fs->Rename("/a", "/a"), 0);
    EXPECT_EQ(ReadAll(*fs, "/a"), "z");
}

TEST(FilesystemRenameTest, MovesTheChangeTimeAndBothDirectoriesTimes) {
    auto fs = InMemoryFileSystem::Create();
    ASSERT_EQ(fs->CreateDirectory("/src", 0), 0);
    ASSERT_EQ(fs->CreateDirectory("/dst", 0), 0);
    ASSERT_TRUE(WriteFile(*fs, "/src/a.txt", "x"));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const FileDateTime before = CurrentFileDateTime();

    ASSERT_EQ(fs->Rename("/src/a.txt", "/dst/a.txt"), 0);

    FileStatus moved;
    ASSERT_EQ(fs->Stat("/dst/a.txt", moved), 0);
    EXPECT_FALSE(moved.changeTime < before);
    FileStatus source, target;
    ASSERT_EQ(fs->Stat("/src", source), 0);
    ASSERT_EQ(fs->Stat("/dst", target), 0);
    EXPECT_FALSE(source.modificationTime < before);
    EXPECT_FALSE(target.modificationTime < before);
}

TEST(FilesystemRenameTest, AcrossAMountIsCrossDevice) {
    auto host = InMemoryFileSystem::Create();
    auto mounted = InMemoryFileSystem::Create();
    host->Mount("/m", mounted);
    ASSERT_TRUE(WriteFile(*host, "/a", "x"));
    ASSERT_TRUE(WriteFile(*mounted, "/x", "y"));

    EXPECT_EQ(host->Rename("/a", "/m/a"), kFileSystemCrossDevice);
    FileStatus status;
    EXPECT_EQ(host->Stat("/a", status), 0); // nothing was moved
    EXPECT_EQ(host->Rename("/m/x", "/y"), kFileSystemCrossDevice);
    EXPECT_EQ(mounted->Stat("/x", status), 0);

    // A missing source is an ordinary failure, as rename() reports ENOENT
    // before EXDEV.
    EXPECT_EQ(host->Rename("/nope", "/m/a"), kFileSystemError);
}

TEST(FilesystemRenameTest, WithinAMountIsRoutedThere) {
    auto host = InMemoryFileSystem::Create();
    auto mounted = MakeInMemoryWith("/x", "data");
    host->Mount("/m", mounted);

    EXPECT_EQ(host->Rename("/m/x", "/m/y"), 0);
    FileStatus status;
    EXPECT_EQ(mounted->Stat("/y", status), 0);
    EXPECT_EQ(host->Stat("/m/x", status), kFileSystemError);
}

TEST(FilesystemRenameTest, AcrossAComposedOverlayIsCrossDevice) {
    auto main = MakeInMemoryWith("/a", "ma");
    ASSERT_TRUE(WriteFile(*main, "/b", "mb"));
    auto overlay = MakeInMemoryWith("/o", "ov");
    auto composed = ComposedFileSystem::Create(main, "/m", overlay);

    // The overlay is a different filesystem: renaming into it is EXDEV, for
    // the caller to answer by copying.
    EXPECT_EQ(composed->Rename("/a", "/m/a"), kFileSystemCrossDevice);
    FileStatus status;
    EXPECT_EQ(main->Stat("/a", status), 0);

    // Main to main is an ordinary rename, landing on main.
    EXPECT_EQ(composed->Rename("/b", "/c"), 0);
    EXPECT_EQ(main->Stat("/c", status), 0);
    EXPECT_EQ(main->Stat("/b", status), kFileSystemError);
}

TEST(FilesystemRenameTest, MountPointsAndTheirParentsDoNotMove) {
    auto host = InMemoryFileSystem::Create();
    host->Mount("/m", InMemoryFileSystem::Create());
    host->Mount("/p/m", InMemoryFileSystem::Create());

    EXPECT_EQ(host->Rename("/m", "/x"), kFileSystemError);
    EXPECT_EQ(host->Rename("/p", "/y"), kFileSystemError);
}

TEST(FilesystemRenameTest, BuiltinsAndPinnedDirectoriesDoNotMove) {
    auto host = InMemoryFileSystem::Create();
    ASSERT_EQ(host->CreateDirectory("/bin", 0), 0);
    ASSERT_EQ(host->AddBuiltinCommand("/bin/ls", "ls"), 0);
    ASSERT_TRUE(WriteFile(*host, "/x", "x"));

    EXPECT_EQ(host->Rename("/bin/ls", "/y"), kFileSystemError);
    EXPECT_EQ(host->Rename("/bin", "/b"), kFileSystemError);
    EXPECT_EQ(host->Rename("/x", "/bin/ls"), kFileSystemError);
}

TEST(FilesystemRenameTest, ReadOnlyAndDeviceRefuse) {
    auto inner = MakeInMemoryWith("/a", "x");
    auto readOnly = ReadOnlyFileSystem::Create(inner);
    EXPECT_EQ(readOnly->Rename("/a", "/b"), kFileSystemError);
    FileStatus status;
    EXPECT_EQ(inner->Stat("/a", status), 0);
    EXPECT_EQ(inner->Stat("/b", status), kFileSystemError);

    auto devices = DeviceFileSystem::Create();
    EXPECT_EQ(devices->Rename("/null", "/x"), kFileSystemError);
}

TEST(FilesystemRenameTest, ASubFileSystemRenamesWithinItsBase) {
    auto root = InMemoryFileSystem::Create();
    ASSERT_EQ(root->CreateDirectory("/base", 0), 0);
    auto sub = SubFileSystem::Create(root, "/base");
    ASSERT_TRUE(WriteFile(*sub, "/a", "x"));

    // ".." stays confined: it resolves to /a of the sub-filesystem, which is
    // /base/a of the root, never to the root's own /b.
    EXPECT_EQ(sub->Rename("/a", "../b"), 0);
    FileStatus status;
    EXPECT_EQ(root->Stat("/base/b", status), 0);
    EXPECT_EQ(root->Stat("/base/a", status), kFileSystemError);
    EXPECT_EQ(root->Stat("/b", status), kFileSystemError);
}

// --- SetTimes ---

TEST(FilesystemSetTimesTest, SetsBothTimes) {
    auto fs = MakeInMemoryWith("/a", "x");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const FileDateTime before = CurrentFileDateTime();
    const FileDateTime access{100, 5};
    const FileDateTime modification{200, 6};

    EXPECT_EQ(fs->SetTimes("/a", access, modification), 0);

    FileStatus status;
    ASSERT_EQ(fs->Stat("/a", status), 0);
    EXPECT_EQ(status.accessTime, access);
    EXPECT_EQ(status.modificationTime, modification);
    EXPECT_FALSE(status.changeTime < before); // ctime becomes now
}

TEST(FilesystemSetTimesTest, NulloptLeavesATimeAlone) {
    auto fs = MakeInMemoryWith("/a", "x");
    const FileDateTime access{100, 5};
    ASSERT_EQ(fs->SetTimes("/a", access, FileDateTime{200, 6}), 0);

    EXPECT_EQ(fs->SetTimes("/a", std::nullopt, FileDateTime{300, 0}), 0);

    FileStatus status;
    ASSERT_EQ(fs->Stat("/a", status), 0);
    EXPECT_EQ(status.accessTime, access); // left alone
    EXPECT_EQ(status.modificationTime, (FileDateTime{300, 0}));
}

TEST(FilesystemSetTimesTest, FailsForAMissingPathABuiltinAndReadOnly) {
    auto fs = MakeInMemoryWith("/a", "x");
    const FileDateTime when{100, 0};

    EXPECT_EQ(fs->SetTimes("/nope", when, when), kFileSystemError);

    ASSERT_EQ(fs->CreateDirectory("/bin", 0), 0);
    ASSERT_EQ(fs->AddBuiltinCommand("/bin/ls", "ls"), 0);
    EXPECT_EQ(fs->SetTimes("/bin/ls", when, when), kFileSystemError);

    auto readOnly = ReadOnlyFileSystem::Create(fs);
    EXPECT_EQ(readOnly->SetTimes("/a", when, when), kFileSystemError);
    FileStatus status;
    ASSERT_EQ(fs->Stat("/a", status), 0);
    EXPECT_FALSE(status.accessTime == when); // the inner one is untouched
}

TEST(FilesystemSetTimesTest, ADeviceKeepsItsTimes) {
    auto devices = DeviceFileSystem::Create();
    FileStatus before;
    ASSERT_EQ(devices->Stat("/null", before), 0);

    EXPECT_EQ(devices->SetTimes("/null", FileDateTime{1, 0}, FileDateTime{2, 0}), 0);

    FileStatus after;
    ASSERT_EQ(devices->Stat("/null", after), 0);
    EXPECT_EQ(after.accessTime, before.accessTime);
    EXPECT_EQ(after.modificationTime, before.modificationTime);
    EXPECT_EQ(devices->SetTimes("/nope", FileDateTime{1, 0}, std::nullopt), kFileSystemError);
}

TEST(FilesystemSetTimesTest, IsRoutedThroughMountsAndSubFileSystems) {
    auto host = InMemoryFileSystem::Create();
    auto mounted = MakeInMemoryWith("/a", "x");
    host->Mount("/m", mounted);
    const FileDateTime access{50, 0};
    const FileDateTime modification{60, 0};

    EXPECT_EQ(host->SetTimes("/m/a", access, modification), 0);
    FileStatus status;
    ASSERT_EQ(mounted->Stat("/a", status), 0);
    EXPECT_EQ(status.accessTime, access);
    EXPECT_EQ(status.modificationTime, modification);

    auto root = InMemoryFileSystem::Create();
    ASSERT_EQ(root->CreateDirectory("/base", 0), 0);
    ASSERT_TRUE(WriteFile(*root, "/base/a", "x"));
    auto sub = SubFileSystem::Create(root, "/base");
    EXPECT_EQ(sub->SetTimes("/a", FileDateTime{70, 0}, FileDateTime{80, 0}), 0);
    ASSERT_EQ(root->Stat("/base/a", status), 0);
    EXPECT_EQ(status.accessTime, (FileDateTime{70, 0}));
    EXPECT_EQ(status.modificationTime, (FileDateTime{80, 0}));
}

// --- The physical filesystem, on the real disk ---

const std::string kPhysicalRoot =
    (std::filesystem::temp_directory_path() / "haisos_fs_rename_test_root").u8string();

class FilesystemPhysicalRenameTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(kPhysicalRoot);
        std::filesystem::create_directories(kPhysicalRoot);
    }

    void TearDown() override {
        std::filesystem::remove_all(kPhysicalRoot);
    }
};

TEST_F(FilesystemPhysicalRenameTest, RenamesAndReplacesOnDisk) {
    auto fs = PhysicalFileSystem::Create(kPhysicalRoot);
    ASSERT_TRUE(WriteFile(*fs, "/a.txt", "1"));
    ASSERT_TRUE(WriteFile(*fs, "/b.txt", "2"));

    EXPECT_EQ(fs->Rename("/a.txt", "/b.txt"), 0);
    EXPECT_FALSE(std::filesystem::exists(kPhysicalRoot + "/a.txt"));
    EXPECT_EQ(ReadHostFile(kPhysicalRoot + "/b.txt"), "1");

    std::filesystem::create_directories(kPhysicalRoot + "/d");
    ASSERT_TRUE(WriteFile(*fs, "/d/f.txt", "in dir"));
    EXPECT_EQ(fs->Rename("/d", "/e"), 0);
    EXPECT_EQ(ReadHostFile(kPhysicalRoot + "/e/f.txt"), "in dir");
    EXPECT_FALSE(std::filesystem::exists(kPhysicalRoot + "/d"));
}

TEST_F(FilesystemPhysicalRenameTest, ReplacesAnEmptyDirectoryOnly) {
    std::filesystem::create_directories(kPhysicalRoot + "/d1");
    std::filesystem::create_directories(kPhysicalRoot + "/d2");
    std::filesystem::create_directories(kPhysicalRoot + "/d3");
    std::ofstream(kPhysicalRoot + "/d3/inside.txt") << "keep";
    auto fs = PhysicalFileSystem::Create(kPhysicalRoot);

    EXPECT_EQ(fs->Rename("/d1", "/d2"), 0);
    EXPECT_TRUE(std::filesystem::exists(kPhysicalRoot + "/d2"));
    EXPECT_FALSE(std::filesystem::exists(kPhysicalRoot + "/d1"));

    std::filesystem::create_directories(kPhysicalRoot + "/d4");
    EXPECT_EQ(fs->Rename("/d4", "/d3"), kFileSystemError);
    EXPECT_EQ(ReadHostFile(kPhysicalRoot + "/d3/inside.txt"), "keep");
    EXPECT_TRUE(std::filesystem::exists(kPhysicalRoot + "/d4"));
}

TEST_F(FilesystemPhysicalRenameTest, RefusesTheRootAndClimbingOut) {
    auto fs = PhysicalFileSystem::Create(kPhysicalRoot);
    ASSERT_TRUE(WriteFile(*fs, "/a", "x"));

    EXPECT_EQ(fs->Rename("/", "/x"), kFileSystemError);
    EXPECT_EQ(fs->Rename("/a", "../a"), kFileSystemError);
    EXPECT_EQ(ReadHostFile(kPhysicalRoot + "/a"), "x");
}

TEST_F(FilesystemPhysicalRenameTest, SetsTimesToTheNanosecondOnLinux) {
    auto fs = PhysicalFileSystem::Create(kPhysicalRoot);
    ASSERT_TRUE(WriteFile(*fs, "/t.txt", "x"));
    const FileDateTime access{1700000000, 123456789};
    const FileDateTime modification{1700000000, 123456789};

    ASSERT_EQ(fs->SetTimes("/t.txt", access, modification), 0);

    FileStatus status;
    ASSERT_EQ(fs->Stat("/t.txt", status), 0);
#ifdef _WIN32
    // _stat64 reports whole seconds, and FILETIME keeps 100 ns of them.
    EXPECT_EQ(status.accessTime, (FileDateTime{1700000000, 0}));
    EXPECT_EQ(status.modificationTime, (FileDateTime{1700000000, 0}));
#else
    EXPECT_EQ(status.accessTime, access);
    EXPECT_EQ(status.modificationTime, modification);
#endif
}

TEST_F(FilesystemPhysicalRenameTest, AcrossTwoPhysicalFilesystemsMountedTogetherIsCrossDevice) {
    const std::string first = kPhysicalRoot + "/first";
    const std::string second = kPhysicalRoot + "/second";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);
    auto fs = PhysicalFileSystem::Create(first);
    fs->Mount("/m", PhysicalFileSystem::Create(second));
    ASSERT_TRUE(WriteFile(*fs, "/a", "x"));

    // Two directories of one disk are one device, but the mount makes them
    // two filesystems: the move across it is EXDEV, not a copy.
    EXPECT_EQ(fs->Rename("/a", "/m/a"), kFileSystemCrossDevice);
    FileStatus status;
    EXPECT_EQ(fs->Stat("/a", status), 0);
}

TEST_F(FilesystemPhysicalRenameTest, RefusesMovingADirectoryBelowItselfAndKeepsTheTarget) {
    std::filesystem::create_directories(kPhysicalRoot + "/d/sub");
    std::ofstream(kPhysicalRoot + "/d/f.txt") << "keep";
    auto fs = PhysicalFileSystem::Create(kPhysicalRoot);

    // rename() answers EINVAL and moves nothing: the empty target directory
    // is still there afterwards, and so is everything below the source. On
    // Linux this documents rename(); on Windows the refusal is made before
    // anything is removed.
    EXPECT_EQ(fs->Rename("/d", "/d/sub"), kFileSystemError);
    EXPECT_TRUE(std::filesystem::is_directory(kPhysicalRoot + "/d/sub"));
    EXPECT_EQ(ReadHostFile(kPhysicalRoot + "/d/f.txt"), "keep");

    // A missing parent below itself fails the same way, moving nothing.
    EXPECT_EQ(fs->Rename("/d", "/d/sub/deeper"), kFileSystemError);
    EXPECT_TRUE(std::filesystem::is_directory(kPhysicalRoot + "/d/sub"));
    EXPECT_EQ(ReadHostFile(kPhysicalRoot + "/d/f.txt"), "keep");
}

#ifdef _WIN32
// A file inside the source, held open without FILE_SHARE_DELETE, makes
// MoveFileExW fail -- after the empty target directory was removed to make
// room. The target must come back as it was: still there, with its times.
TEST_F(FilesystemPhysicalRenameTest, AFailedMoveKeepsTheEmptyTarget) {
    std::filesystem::create_directories(kPhysicalRoot + "/src");
    std::filesystem::create_directories(kPhysicalRoot + "/dst");
    std::ofstream(kPhysicalRoot + "/src/held.txt") << "held";
    auto fs = PhysicalFileSystem::Create(kPhysicalRoot);
    const FileDateTime when{1600000000, 0};
    ASSERT_EQ(fs->SetTimes("/dst", when, when), 0);

    std::wstring heldWide;
    ASSERT_TRUE(Utf8ToWide(kPhysicalRoot + "/src/held.txt", heldWide));
    const HANDLE held = ::CreateFileW(heldWide.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE /* no FILE_SHARE_DELETE */,
        nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(held, INVALID_HANDLE_VALUE);
    const int result = fs->Rename("/src", "/dst");
    ::CloseHandle(held);

    // Either the move failed and the target is intact, or a Windows version
    // let it happen and the moved file is there instead.
    if (result == 0) {
        EXPECT_EQ(ReadHostFile(kPhysicalRoot + "/dst/held.txt"), "held");
        return;
    }
    EXPECT_TRUE(std::filesystem::is_directory(kPhysicalRoot + "/dst"));
    FileStatus status;
    ASSERT_EQ(fs->Stat("/dst", status), 0);
    EXPECT_EQ(status.modificationTime, when);
    EXPECT_EQ(ReadHostFile(kPhysicalRoot + "/src/held.txt"), "held");
}
#endif

TEST_F(FilesystemPhysicalRenameTest, SetTimesRefusesATimeBeyondFiletime) {
    auto fs = PhysicalFileSystem::Create(kPhysicalRoot);
    ASSERT_TRUE(WriteFile(*fs, "/t.txt", "x"));
    FileStatus before;
    ASSERT_EQ(fs->Stat("/t.txt", before), 0);

    // INT64_MAX/2 seconds since the epoch is beyond the last FILETIME: the
    // call fails rather than overflowing. On Windows that is certain; on
    // Linux whatever utimensat answers, nothing may change when it fails.
    const int result = fs->SetTimes("/t.txt", FileDateTime{INT64_MAX / 2, 0}, std::nullopt);
#ifdef _WIN32
    EXPECT_EQ(result, kFileSystemError);
#endif
    if (result != 0) {
        FileStatus after;
        ASSERT_EQ(fs->Stat("/t.txt", after), 0);
        EXPECT_EQ(after.modificationTime, before.modificationTime);
    }
}