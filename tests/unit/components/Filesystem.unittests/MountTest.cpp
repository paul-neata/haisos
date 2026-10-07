#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Filesystem/InMemoryFileSystem.h"

using namespace Haisos;

namespace {

std::shared_ptr<IFileSystem> MakeInMemoryWith(const std::string& path, const std::string& contents) {
    auto fs = InMemoryFileSystem::Create();
    auto file = fs->OpenFile(path, kFileOpenWriteCreateTruncate);
    EXPECT_NE(file, nullptr);
    if (file) {
        file->Write(contents.data(), contents.size());
    }
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
    auto hostFile = host->OpenFile("/own.txt", kFileOpenWriteCreateTruncate);
    ASSERT_NE(hostFile, nullptr);
    const std::string own = "host file";
    hostFile->Write(own.data(), own.size());
    hostFile.reset();

    host->Mount("/data", MakeInMemoryWith("/note.txt", "mounted"));

    EXPECT_EQ(ReadAll(*host, "/own.txt"), "host file");
    EXPECT_EQ(ReadAll(*host, "/data/note.txt"), "mounted");
}

TEST(MountTest, WritesUnderAMountPointLandOnTheMountedFilesystem) {
    auto mounted = InMemoryFileSystem::Create();
    auto host = InMemoryFileSystem::Create();
    host->Mount("/scratch", mounted);

    auto file = host->OpenFile("/scratch/out.txt", kFileOpenWriteCreateTruncate);
    ASSERT_NE(file, nullptr);
    const std::string payload = "written through the mount";
    EXPECT_EQ(file->Write(payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));

    // The bytes must be on the mounted filesystem, not the host's own storage:
    // each call is routed to whichever filesystem owns the path.
    EXPECT_EQ(ReadAll(*mounted, "/out.txt"), payload);
}

TEST(MountTest, HostAndMountedFilesReadThroughTheirOwnDescriptors) {
    // A mounted file's open is routed to the mounted filesystem, which hands
    // back its own descriptor; each descriptor then reads its own file.
    auto host = InMemoryFileSystem::Create();
    auto hostFile = host->OpenFile("/host.txt", kFileOpenWriteCreateTruncate);
    ASSERT_NE(hostFile, nullptr);
    const std::string hostPayload = "host";
    hostFile->Write(hostPayload.data(), hostPayload.size());
    hostFile.reset();

    host->Mount("/m", MakeInMemoryWith("/mounted.txt", "mounted"));

    auto a = host->OpenFile("/host.txt", kFileOpenReadOnly);
    auto b = host->OpenFile("/m/mounted.txt", kFileOpenReadOnly);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    char bufA[16] = {};
    char bufB[16] = {};
    EXPECT_GT(a->Read(bufA, sizeof(bufA) - 1), 0);
    EXPECT_GT(b->Read(bufB, sizeof(bufB) - 1), 0);
    EXPECT_STREQ(bufA, "host");
    EXPECT_STREQ(bufB, "mounted");
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
