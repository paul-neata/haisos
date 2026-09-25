#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Filesystem/InMemoryFileSystem.h"

using namespace Haisos;

namespace {

std::shared_ptr<IFileSystem> MakeInMemoryWith(const std::string& path, const std::string& contents) {
    auto fs = InMemoryFileSystem::Create();
    int fd = fs->OpenFile(path, kFileOpenWriteCreateTruncate);
    EXPECT_GE(fd, 0);
    fs->WriteFile(fd, contents.data(), contents.size());
    fs->CloseFile(fd);
    return fs;
}

std::string ReadAll(IFileSystem& fs, const std::string& path) {
    std::string content;
    return ReadWholeFile(fs, path, content) ? content : std::string("<unreadable>");
}

} // namespace

TEST(MountTest, MountedPathsAreServedByTheMountedFilesystem) {
    auto host = InMemoryFileSystem::Create();
    host->Mount("/data", MakeInMemoryWith("/note.txt", "from the mount"));

    EXPECT_EQ(ReadAll(*host, "/data/note.txt"), "from the mount");
}

TEST(MountTest, MountDoesNotDisturbTheHostsOwnPaths) {
    auto host = InMemoryFileSystem::Create();
    int fd = host->OpenFile("/own.txt", kFileOpenWriteCreateTruncate);
    ASSERT_GE(fd, 0);
    const std::string own = "host file";
    host->WriteFile(fd, own.data(), own.size());
    host->CloseFile(fd);

    host->Mount("/data", MakeInMemoryWith("/note.txt", "mounted"));

    EXPECT_EQ(ReadAll(*host, "/own.txt"), "host file");
    EXPECT_EQ(ReadAll(*host, "/data/note.txt"), "mounted");
}

TEST(MountTest, WritesUnderAMountPointLandOnTheMountedFilesystem) {
    auto mounted = InMemoryFileSystem::Create();
    auto host = InMemoryFileSystem::Create();
    host->Mount("/scratch", mounted);

    int fd = host->OpenFile("/scratch/out.txt", kFileOpenWriteCreateTruncate);
    ASSERT_GE(fd, 0);
    const std::string payload = "written through the mount";
    EXPECT_EQ(host->WriteFile(fd, payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));
    EXPECT_EQ(host->CloseFile(fd), 0);

    // The bytes must be on the mounted filesystem, not the host's own storage:
    // each call is routed to whichever filesystem owns the path.
    EXPECT_EQ(ReadAll(*mounted, "/out.txt"), payload);
}

TEST(MountTest, HostAndMountedDescriptorsDoNotCollide) {
    // Both filesystems hand out descriptors from their own namespaces, starting
    // at the same number, so an unmapped fd would route a read to the wrong file.
    auto host = InMemoryFileSystem::Create();
    int hostFd = host->OpenFile("/host->txt", kFileOpenWriteCreateTruncate);
    ASSERT_GE(hostFd, 0);
    const std::string hostPayload = "host";
    host->WriteFile(hostFd, hostPayload.data(), hostPayload.size());
    host->CloseFile(hostFd);

    host->Mount("/m", MakeInMemoryWith("/mounted.txt", "mounted"));

    int a = host->OpenFile("/host->txt", kFileOpenReadOnly);
    int b = host->OpenFile("/m/mounted.txt", kFileOpenReadOnly);
    ASSERT_GE(a, 0);
    ASSERT_GE(b, 0);
    EXPECT_NE(a, b);

    char bufA[16] = {};
    char bufB[16] = {};
    EXPECT_GT(host->ReadFile(a, bufA, sizeof(bufA) - 1), 0);
    EXPECT_GT(host->ReadFile(b, bufB, sizeof(bufB) - 1), 0);
    EXPECT_STREQ(bufA, "host");
    EXPECT_STREQ(bufB, "mounted");
    host->CloseFile(a);
    host->CloseFile(b);
}

TEST(MountTest, ListingAnAncestorShowsTheWayToAMountPoint) {
    auto host = InMemoryFileSystem::Create();
    host->Mount("/mnt/data", MakeInMemoryWith("/note.txt", "x"));

    auto root = host->ReadDirectory("/");
    bool sawMnt = false;
    for (const auto& entry : root) {
        if (entry.name == "mnt") {
            sawMnt = true;
            EXPECT_EQ(entry.type, DirectoryEntryType::Dir);
        }
    }
    EXPECT_TRUE(sawMnt) << "the mount point must be reachable by listing down from the root";
}

TEST(MountTest, UnmountRestoresTheHostsOwnView) {
    auto host = InMemoryFileSystem::Create();
    host->Mount("/data", MakeInMemoryWith("/note.txt", "mounted"));
    ASSERT_EQ(ReadAll(*host, "/data/note.txt"), "mounted");

    host->Unmount("/data");

    EXPECT_EQ(ReadAll(*host, "/data/note.txt"), "<unreadable>");
}

TEST(MountTest, UnmountingAPathThatIsNotAMountPointDoesNothing) {
    auto host = InMemoryFileSystem::Create();
    host->Mount("/data", MakeInMemoryWith("/note.txt", "mounted"));

    host->Unmount("/not-a-mount");

    EXPECT_EQ(ReadAll(*host, "/data/note.txt"), "mounted");
}

TEST(MountTest, NestedMountsResolveToTheInnermost) {
    auto host = InMemoryFileSystem::Create();
    host->Mount("/a", MakeInMemoryWith("/f.txt", "outer"));
    host->Mount("/a/b", MakeInMemoryWith("/f.txt", "inner"));

    EXPECT_EQ(ReadAll(*host, "/a/f.txt"), "outer");
    EXPECT_EQ(ReadAll(*host, "/a/b/f.txt"), "inner");
}

TEST(MountTest, MountingOverAnExistingMountPointReplacesIt) {
    auto host = InMemoryFileSystem::Create();
    host->Mount("/data", MakeInMemoryWith("/note.txt", "first"));
    host->Mount("/data", MakeInMemoryWith("/note.txt", "second"));

    EXPECT_EQ(ReadAll(*host, "/data/note.txt"), "second");
}

TEST(MountTest, RemoveFileIsRoutedToTheMountedFilesystem) {
    auto mounted = MakeInMemoryWith("/note.txt", "from the mount");
    auto host = MakeInMemoryWith("/note.txt", "from the host");
    host->Mount("/data", mounted);

    EXPECT_EQ(host->RemoveFile("/data/note.txt"), 0);
    EXPECT_EQ(ReadAll(*mounted, "/note.txt"), "<unreadable>");
    // The host's own file of the same name is untouched.
    EXPECT_EQ(ReadAll(*host, "/note.txt"), "from the host");
}

TEST(MountTest, InMemoryRemoveFileRefusesDirectoriesAndMissingFiles) {
    auto fs = MakeInMemoryWith("/note.txt", "x");
    ASSERT_EQ(fs->CreateDirectory("/dir", 0), 0);

    EXPECT_LT(fs->RemoveFile("/dir"), 0);
    EXPECT_LT(fs->RemoveFile("/missing.txt"), 0);
    EXPECT_EQ(fs->RemoveFile("/note.txt"), 0);
    EXPECT_EQ(ReadAll(*fs, "/note.txt"), "<unreadable>");
}
