#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "src/components/Filesystem/ComposedFileSystem.h"
#include "src/components/Filesystem/DeviceFileSystem.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Filesystem/InMemoryFileSystem.h"
#include "src/components/Filesystem/ReadOnlyFileSystem.h"
#include "src/components/Filesystem/SubFileSystem.h"

using namespace Haisos;

namespace {

#ifdef _WIN32
constexpr int kDirMode = _S_IREAD | _S_IWRITE;
#else
constexpr int kDirMode = S_IRWXU;
#endif

std::vector<std::string> Names(const std::vector<DirectoryEntry>& entries) {
    std::vector<std::string> names;
    for (const auto& entry : entries) {
        names.push_back(entry.name);
    }
    return names;
}

// An in-memory root with a device filesystem mounted at /dev, as the
// haisosfile's "MOUNT rootfs /dev devfs" makes it.
std::shared_ptr<InMemoryFileSystem> RootWithDev() {
    auto root = InMemoryFileSystem::Create();
    root->Mount("/dev", DeviceFileSystem::Create());
    return root;
}

} // namespace

using Names_t = std::vector<std::string>;

TEST(DeviceFileSystemTest, ListsItsDevicesAsCharacterDevices) {
    auto fs = DeviceFileSystem::Create();
    const auto entries = fs->ReadDirectory("/");
    EXPECT_EQ(Names(entries), (Names_t{".", "..", "null", "zero"}));
    EXPECT_EQ(entries[0].type, DirectoryEntryType::Dir);
    EXPECT_EQ(entries[1].type, DirectoryEntryType::Dir);
    EXPECT_EQ(entries[2].type, DirectoryEntryType::CharDevice);
    EXPECT_EQ(entries[3].type, DirectoryEntryType::CharDevice);
    // A device is no directory, and nothing else is there.
    EXPECT_TRUE(fs->ReadDirectory("/null").empty());
    EXPECT_TRUE(fs->ReadDirectory("/missing").empty());
}

TEST(DeviceFileSystemTest, StatReportsLinuxsDeviceNumbers) {
    auto fs = DeviceFileSystem::Create();
    FileStatus status;
    ASSERT_EQ(fs->Stat("/null", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::CharDevice);
    EXPECT_EQ(status.size, 0u);
    EXPECT_EQ(status.blocks, 0u);
    EXPECT_EQ(status.linkCount, 1u);
    EXPECT_EQ(status.deviceMajor, 1u);
    EXPECT_EQ(status.deviceMinor, 3u);

    ASSERT_EQ(fs->Stat("/zero", status), 0);
    EXPECT_EQ(status.deviceMajor, 1u);
    EXPECT_EQ(status.deviceMinor, 5u);

    ASSERT_EQ(fs->Stat("/", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
    EXPECT_EQ(status.deviceMajor, 0u);

    EXPECT_EQ(fs->Stat("/missing", status), -1);
    EXPECT_EQ(EntryTypeOf(*fs, "/null"), std::optional<char>(DirectoryEntryType::CharDevice));
}

TEST(DeviceFileSystemTest, NullDiscardsWritesAndReadsAsEndOfFile) {
    auto fs = DeviceFileSystem::Create();
    // As "> /dev/null" and ">> /dev/null" open it.
    for (int flags : {kFileOpenWriteCreateTruncate, kFileOpenWriteCreateAppend}) {
        const int fd = fs->OpenFile("/null", flags, kFileCreateMode);
        ASSERT_GE(fd, 0);
        EXPECT_EQ(fs->WriteFile(fd, "discarded", 9), 9);
        EXPECT_EQ(fs->CloseFile(fd), 0);
    }
    const int fd = fs->OpenFile("/null", kFileOpenReadOnly);
    ASSERT_GE(fd, 0);
    char buffer[16];
    EXPECT_EQ(fs->ReadFile(fd, buffer, sizeof(buffer)), 0);
    EXPECT_EQ(fs->CloseFile(fd), 0);

    std::string content = "x";
    ASSERT_TRUE(ReadWholeFile(*fs, "/null", content));
    EXPECT_EQ(content, "");
}

TEST(DeviceFileSystemTest, ZeroDiscardsWritesAndReadsEndlessZeroBytes) {
    auto fs = DeviceFileSystem::Create();
    const int fd = fs->OpenFile("/zero", kFileReadWriteBit);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(fs->WriteFile(fd, "discarded", 9), 9);
    for (int i = 0; i < 3; ++i) {
        std::vector<char> buffer(4096, 'x');
        ASSERT_EQ(fs->ReadFile(fd, buffer.data(), buffer.size()), 4096);
        EXPECT_EQ(buffer, std::vector<char>(4096, '\0'));
    }
    EXPECT_EQ(fs->CloseFile(fd), 0);
    // Closed, it reads and writes no more.
    char c = 'x';
    EXPECT_EQ(fs->ReadFile(fd, &c, 1), -1);
    EXPECT_EQ(fs->WriteFile(fd, &c, 1), -1);
    EXPECT_EQ(fs->CloseFile(fd), -1);
}

TEST(DeviceFileSystemTest, NothingCanBeCreatedOrRemoved) {
    auto fs = DeviceFileSystem::Create();
    EXPECT_EQ(fs->OpenFile("/new", kFileOpenWriteCreateTruncate, kFileCreateMode), -1);
    EXPECT_EQ(fs->OpenFile("/", kFileOpenReadOnly), -1);
    EXPECT_EQ(fs->CreateDirectory("/dir", kDirMode), -1);
    EXPECT_EQ(fs->RemoveFile("/null"), -1);
    EXPECT_EQ(fs->RemoveDirectory("/"), -1);
    EXPECT_EQ(fs->AddBuiltinCommand("/echo", "echo"), -1);
    EXPECT_FALSE(fs->IsBuiltinCommand("/echo").has_value());
    EXPECT_EQ(Names(fs->ReadDirectory("/")), (Names_t{".", "..", "null", "zero"}));
}

TEST(DeviceFileSystemTest, WorksMountedAtDev) {
    auto root = RootWithDev();
    EXPECT_EQ(Names(root->ReadDirectory("/")), (Names_t{".", "..", "dev"}));
    EXPECT_EQ(Names(root->ReadDirectory("/dev")), (Names_t{".", "..", "null", "zero"}));
    EXPECT_EQ(EntryTypeOf(*root, "/dev/null"), std::optional<char>(DirectoryEntryType::CharDevice));

    const int out = root->OpenFile("/dev/null", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_GE(out, 0);
    EXPECT_EQ(root->WriteFile(out, "abc", 3), 3);
    EXPECT_EQ(root->CloseFile(out), 0);

    const int in = root->OpenFile("/dev/zero", kFileOpenReadOnly);
    ASSERT_GE(in, 0);
    char buffer[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    EXPECT_EQ(root->ReadFile(in, buffer, sizeof(buffer)), 8);
    EXPECT_EQ(std::string(buffer, 8), std::string(8, '\0'));
    EXPECT_EQ(root->CloseFile(in), 0);

    EXPECT_EQ(root->OpenFile("/dev/new", kFileOpenWriteCreateTruncate, kFileCreateMode), -1);
    EXPECT_EQ(root->CreateDirectory("/dev/dir", kDirMode), -1);
    EXPECT_EQ(root->RemoveFile("/dev/zero"), -1);
}

// --- "." and ".." in every listing ---

TEST(DotEntriesTest, EveryDirectoryListsDotAndDotDotFirstOnce) {
    auto mem = InMemoryFileSystem::Create();
    ASSERT_EQ(mem->CreateDirectory("/a", kDirMode), 0);
    ASSERT_EQ(mem->CreateDirectory("/a/b", kDirMode), 0);
    const int fd = mem->OpenFile("/a/file", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_GE(fd, 0);
    mem->CloseFile(fd);

    EXPECT_EQ(Names(mem->ReadDirectory("/")), (Names_t{".", "..", "a"}));
    auto inA = Names(mem->ReadDirectory("/a"));
    ASSERT_EQ(inA.size(), 4u);
    EXPECT_EQ(inA[0], ".");
    EXPECT_EQ(inA[1], "..");
    // An empty directory still has them; a file or nothing has none.
    EXPECT_EQ(Names(mem->ReadDirectory("/a/b")), (Names_t{".", ".."}));
    EXPECT_TRUE(mem->ReadDirectory("/a/file").empty());
    EXPECT_TRUE(mem->ReadDirectory("/missing").empty());

    // Through every view, and a mount on a view, still once and first.
    auto readOnly = ReadOnlyFileSystem::Create(mem);
    EXPECT_EQ(Names(readOnly->ReadDirectory("/a/b")), (Names_t{".", ".."}));
    auto sub = SubFileSystem::Create(mem, "/a");
    EXPECT_EQ(Names(sub->ReadDirectory("/b")), (Names_t{".", ".."}));
    auto composed = ComposedFileSystem::Create(readOnly, "/dev", DeviceFileSystem::Create());
    EXPECT_EQ(Names(composed->ReadDirectory("/")), (Names_t{".", "..", "a", "dev"}));
    EXPECT_EQ(Names(composed->ReadDirectory("/dev")), (Names_t{".", "..", "null", "zero"}));
}

TEST(DotEntriesTest, TheWayDownToAMountPointListsThemToo) {
    auto root = InMemoryFileSystem::Create();
    root->Mount("/mnt/deep/dev", DeviceFileSystem::Create());
    EXPECT_EQ(Names(root->ReadDirectory("/mnt")), (Names_t{".", "..", "deep"}));
    EXPECT_EQ(Names(root->ReadDirectory("/mnt/deep")), (Names_t{".", "..", "dev"}));
}
