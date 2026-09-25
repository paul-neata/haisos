#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include "src/components/Filesystem/BuiltinCommandFile.h"
#include "src/components/Filesystem/ComposedFileSystem.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Filesystem/InMemoryFileSystem.h"
#include "src/components/Filesystem/PhysicalFileSystem.h"
#include "src/components/Filesystem/ReadOnlyFileSystem.h"
#include "src/components/Filesystem/SubFileSystem.h"

using namespace Haisos;

namespace {

#ifdef _WIN32
constexpr int kDirMode = _S_IREAD | _S_IWRITE;
#else
constexpr int kDirMode = S_IRWXU;
#endif

const std::string kPhysicalRoot = "/tmp/haisos_stat_test";

void Write(IFileSystem& fs, const std::string& path, const std::string& content) {
    const int fd = fs.OpenFile(path, kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_GE(fd, 0) << path;
    ASSERT_EQ(fs.WriteFile(fd, content.data(), content.size()), static_cast<ssize_t>(content.size()));
    fs.CloseFile(fd);
}

// Seconds either side of now that a freshly written file's time may fall in.
constexpr int64_t kSlack = 5;

bool IsRecent(const FileDateTime& when) {
    const int64_t time = when.seconds;
    const int64_t now = CurrentFileDateTime().seconds;
    return time >= now - kSlack && time <= now + kSlack;
}

} // namespace

TEST(StatTest, InMemoryReportsTypeSizeAndTime) {
    auto fs = InMemoryFileSystem::Create();
    ASSERT_EQ(fs->CreateDirectory("/dir", kDirMode), 0);
    Write(*fs, "/dir/file.txt", "12345");

    FileStatus status;
    ASSERT_EQ(fs->Stat("/dir/file.txt", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::File);
    EXPECT_EQ(status.size, 5u);
    EXPECT_TRUE(IsRecent(status.modificationTime)) << status.modificationTime.seconds;

    ASSERT_EQ(fs->Stat("/dir", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
    EXPECT_TRUE(IsRecent(status.modificationTime));

    ASSERT_EQ(fs->Stat("/", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
}

TEST(StatTest, AMissingPathFailsAndLeavesTheResultAlone) {
    auto fs = InMemoryFileSystem::Create();
    FileStatus status;
    status.size = 42;
    status.modificationTime = FileDateTime{7, 8};
    EXPECT_EQ(fs->Stat("/nothing", status), -1);
    EXPECT_EQ(status.size, 42u);
    EXPECT_EQ(status.modificationTime, (FileDateTime{7, 8}));
}

TEST(StatTest, InMemorySizeFollowsWritesAndTruncation) {
    auto fs = InMemoryFileSystem::Create();
    Write(*fs, "/f", "a longer text");
    FileStatus status;
    ASSERT_EQ(fs->Stat("/f", status), 0);
    EXPECT_EQ(status.size, 13u);
    Write(*fs, "/f", "short");
    ASSERT_EQ(fs->Stat("/f", status), 0);
    EXPECT_EQ(status.size, 5u);
}

TEST(StatTest, PhysicalReportsWhatIsOnDisk) {
    std::filesystem::remove_all(kPhysicalRoot);
    std::filesystem::create_directories(kPhysicalRoot + "/sub");
    std::ofstream(kPhysicalRoot + "/sub/data.bin", std::ios::binary) << std::string(1000, 'x');
    auto fs = PhysicalFileSystem::Create(kPhysicalRoot);

    FileStatus status;
    ASSERT_EQ(fs->Stat("/sub/data.bin", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::File);
    EXPECT_EQ(status.size, 1000u);
    EXPECT_TRUE(IsRecent(status.modificationTime)) << status.modificationTime.seconds;
    ASSERT_EQ(fs->Stat("/sub", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
    EXPECT_EQ(fs->Stat("/sub/missing", status), -1);
    // Escaping the jail is not a way to learn about the host.
    EXPECT_EQ(fs->Stat("/../../etc", status), -1);
    std::filesystem::remove_all(kPhysicalRoot);
}

TEST(StatTest, ViewsReportWhatTheyWrap) {
    auto inner = InMemoryFileSystem::Create();
    ASSERT_EQ(inner->CreateDirectory("/base", kDirMode), 0);
    Write(*inner, "/base/f", "abc");

    FileStatus status;
    auto readOnly = ReadOnlyFileSystem::Create(inner);
    ASSERT_EQ(readOnly->Stat("/base/f", status), 0);
    EXPECT_EQ(status.size, 3u);

    auto sub = SubFileSystem::Create(inner, "/base");
    ASSERT_EQ(sub->Stat("/f", status), 0);
    EXPECT_EQ(status.size, 3u);
    EXPECT_EQ(sub->Stat("/base", status), -1);

    auto composed = ComposedFileSystem::Create(InMemoryFileSystem::Create(), "/deep/mnt", inner);
    ASSERT_EQ(composed->Stat("/deep/mnt/base/f", status), 0);
    EXPECT_EQ(status.size, 3u);
}

TEST(StatTest, MountPointsAndTheWayDownToThemAreDirectories) {
    // Neither /a nor /a/b exists in the host: they are there because a mount
    // is, the same as a listing shows them.
    auto host = InMemoryFileSystem::Create();
    host->Mount("/a/b", InMemoryFileSystem::Create());
    FileStatus status;
    ASSERT_EQ(host->Stat("/a", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
    ASSERT_EQ(host->Stat("/a/b", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
    EXPECT_EQ(host->Stat("/a/c", status), -1);
}

TEST(StatTest, ABuiltinIsAFileSizedLikeItsNoteAndTimedFromItsPlacing) {
    auto fs = InMemoryFileSystem::Create();
    ASSERT_EQ(fs->CreateDirectory("/bin", kDirMode), 0);
    ASSERT_EQ(fs->AddBuiltinCommand("/bin/ls", "ls"), 0);
    FileStatus status;
    ASSERT_EQ(fs->Stat("/bin/ls", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::File);
    EXPECT_EQ(status.size, BuiltinCommandFileContent("ls").size());
    EXPECT_TRUE(IsRecent(status.modificationTime));
    // And seen the same way through a view.
    ASSERT_EQ(ReadOnlyFileSystem::Create(fs)->Stat("/bin/ls", status), 0);
    EXPECT_EQ(status.size, BuiltinCommandFileContent("ls").size());
}

TEST(StatTest, EntryTypeOfAgreesWithStat) {
    auto fs = InMemoryFileSystem::Create();
    ASSERT_EQ(fs->CreateDirectory("/d", kDirMode), 0);
    Write(*fs, "/d/f", "x");
    EXPECT_EQ(EntryTypeOf(*fs, "/d"), std::optional<char>(DirectoryEntryType::Dir));
    EXPECT_EQ(EntryTypeOf(*fs, "/d/f"), std::optional<char>(DirectoryEntryType::File));
    EXPECT_EQ(EntryTypeOf(*fs, "/d/g"), std::nullopt);
}

TEST(StatTest, FileDateTimeComparesChronologically) {
    EXPECT_LT((FileDateTime{1, 999999999}), (FileDateTime{2, 0}));
    EXPECT_LT((FileDateTime{2, 1}), (FileDateTime{2, 2}));
    EXPECT_GT((FileDateTime{3, 0}), (FileDateTime{2, 5}));
    EXPECT_EQ((FileDateTime{4, 4}), (FileDateTime{4, 4}));
    EXPECT_NE((FileDateTime{4, 4}), (FileDateTime{4, 5}));
    EXPECT_LE((FileDateTime{4, 4}), (FileDateTime{4, 4}));
    const FileDateTime now = CurrentFileDateTime();
    EXPECT_LT(now.nanoseconds, 1000000000u);
}

TEST(StatTest, InMemoryKeepsAccessModificationAndChangeTimes) {
    auto fs = InMemoryFileSystem::Create();
    Write(*fs, "/f", "data");
    FileStatus before;
    ASSERT_EQ(fs->Stat("/f", before), 0);
    EXPECT_TRUE(IsRecent(before.accessTime));
    EXPECT_TRUE(IsRecent(before.changeTime));
    EXPECT_GE(before.changeTime, before.modificationTime);

    // Reading moves the access time only.
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*fs, "/f", content));
    FileStatus afterRead;
    ASSERT_EQ(fs->Stat("/f", afterRead), 0);
    EXPECT_GE(afterRead.accessTime, before.accessTime);
    EXPECT_EQ(afterRead.modificationTime, before.modificationTime);
    EXPECT_EQ(afterRead.changeTime, before.changeTime);

    // Writing moves the modification and change times.
    Write(*fs, "/f", "more data");
    FileStatus afterWrite;
    ASSERT_EQ(fs->Stat("/f", afterWrite), 0);
    EXPECT_GE(afterWrite.modificationTime, before.modificationTime);
    EXPECT_GE(afterWrite.changeTime, afterWrite.modificationTime);
}

TEST(StatTest, ADirectorysTimeMovesWhenItsEntriesChange) {
    auto fs = InMemoryFileSystem::Create();
    ASSERT_EQ(fs->CreateDirectory("/d", kDirMode), 0);
    FileStatus before;
    ASSERT_EQ(fs->Stat("/d", before), 0);
    Write(*fs, "/d/new", "x");
    FileStatus after;
    ASSERT_EQ(fs->Stat("/d", after), 0);
    EXPECT_GE(after.modificationTime, before.modificationTime);
}

TEST(StatTest, LinkCountsAndBlocksAreThoseOfARealFilesystem) {
    auto fs = InMemoryFileSystem::Create();
    ASSERT_EQ(fs->CreateDirectory("/d", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/d/a", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/d/b", kDirMode), 0);
    Write(*fs, "/d/f", std::string(1000, 'x'));

    FileStatus status;
    ASSERT_EQ(fs->Stat("/d", status), 0);
    EXPECT_EQ(status.linkCount, 4u); // ".", its entry in "/", and a/.. and b/..
    ASSERT_EQ(fs->Stat("/d/a", status), 0);
    EXPECT_EQ(status.linkCount, 2u);
    ASSERT_EQ(fs->Stat("/d/f", status), 0);
    EXPECT_EQ(status.linkCount, 1u);
    EXPECT_EQ(status.blocks, 2u); // 1000 bytes in 512-byte blocks
    EXPECT_EQ(BlocksForSize(0), 0u);
    EXPECT_EQ(BlocksForSize(512), 1u);
    EXPECT_EQ(BlocksForSize(513), 2u);
}

TEST(StatTest, APhysicalFileHasItsDiskTimesToTheNanosecondAndItsBlocks) {
    std::filesystem::remove_all(kPhysicalRoot);
    std::filesystem::create_directories(kPhysicalRoot);
    std::ofstream(kPhysicalRoot + "/f") << std::string(5000, 'x');
    auto fs = PhysicalFileSystem::Create(kPhysicalRoot);
    FileStatus status;
    ASSERT_EQ(fs->Stat("/f", status), 0);
    EXPECT_GE(status.blocks, 10u); // at least the 5000 bytes, in 512-byte blocks
    EXPECT_EQ(status.linkCount, 1u);
    EXPECT_TRUE(IsRecent(status.accessTime));
    EXPECT_TRUE(IsRecent(status.changeTime));
    EXPECT_LT(status.modificationTime.nanoseconds, 1000000000u);
    std::filesystem::remove_all(kPhysicalRoot);
}

TEST(StatTest, ADirectoryLeadingToAMountIsTimedFromTheMount) {
    auto host = InMemoryFileSystem::Create();
    host->Mount("/a/b", InMemoryFileSystem::Create());
    FileStatus status;
    ASSERT_EQ(host->Stat("/a", status), 0);
    EXPECT_TRUE(IsRecent(status.modificationTime));
    EXPECT_EQ(status.linkCount, 3u);
}

TEST(StatTest, ABuiltinTakesNoBlocks) {
    auto fs = InMemoryFileSystem::Create();
    ASSERT_EQ(fs->AddBuiltinCommand("/ls", "ls"), 0);
    FileStatus status;
    ASSERT_EQ(fs->Stat("/ls", status), 0);
    EXPECT_EQ(status.blocks, 0u);
    EXPECT_EQ(status.accessTime, status.modificationTime);
}
