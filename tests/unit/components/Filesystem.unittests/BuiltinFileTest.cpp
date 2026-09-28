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

// An in-memory filesystem with /bin, and the echo builtin placed at /bin/echo.
std::shared_ptr<InMemoryFileSystem> WithEchoInBin() {
    auto fs = InMemoryFileSystem::Create();
    EXPECT_EQ(fs->CreateDirectory("/bin", kDirMode), 0);
    EXPECT_EQ(fs->AddBuiltinCommand("/bin/echo", "echo"), 0);
    return fs;
}

std::string ReadAll(IFileSystem& fs, const std::string& path) {
    std::string content;
    return ReadWholeFile(fs, path, content) ? content : std::string("<unreadable>");
}

bool Lists(IFileSystem& fs, const std::string& directory, const std::string& name, char type) {
    for (const auto& entry : fs.ReadDirectory(directory)) {
        if (entry.name == name) {
            return entry.type == type;
        }
    }
    return false;
}

} // namespace

TEST(BuiltinFileTest, IsBuiltinCommandNamesTheBuiltin) {
    auto fs = WithEchoInBin();
    EXPECT_EQ(fs->IsBuiltinCommand("/bin/echo").value_or(""), "echo");
    // Paths are normalized, as for every other operation.
    EXPECT_EQ(fs->IsBuiltinCommand("/bin/../bin/./echo").value_or(""), "echo");
    EXPECT_FALSE(fs->IsBuiltinCommand("/bin/cat").has_value());
    EXPECT_FALSE(fs->IsBuiltinCommand("/bin").has_value());
}

TEST(BuiltinFileTest, ABuiltinListsAsAFile) {
    auto fs = WithEchoInBin();
    EXPECT_TRUE(Lists(*fs, "/bin", "echo", DirectoryEntryType::File));
}

TEST(BuiltinFileTest, ReadingABuiltinGivesItsNote) {
    auto fs = WithEchoInBin();
    EXPECT_EQ(ReadAll(*fs, "/bin/echo"), BuiltinCommandFileContent("echo"));
    EXPECT_NE(BuiltinCommandFileContent("echo").find("builtin command echo"), std::string::npos);
}

TEST(BuiltinFileTest, ABuiltinCannotBeWrittenCreatedOverOrRemoved) {
    auto fs = WithEchoInBin();
    EXPECT_LT(fs->OpenFile("/bin/echo", kFileOpenWriteCreateTruncate, kFileCreateMode), 0);
    EXPECT_LT(fs->OpenFile("/bin/echo", kFileOpenWriteCreateAppend), 0);
    EXPECT_NE(fs->CreateDirectory("/bin/echo", kDirMode), 0);
    EXPECT_NE(fs->RemoveFile("/bin/echo"), 0);
    // Still there, and still the builtin.
    EXPECT_EQ(fs->IsBuiltinCommand("/bin/echo").value_or(""), "echo");
}

TEST(BuiltinFileTest, AReadDescriptorOfABuiltinCannotBeWrittenThrough) {
    auto fs = WithEchoInBin();
    const int fd = fs->OpenFile("/bin/echo", kFileOpenReadOnly);
    ASSERT_GE(fd, 0);
    EXPECT_LT(fs->WriteFile(fd, "x", 1), 0);
    EXPECT_EQ(fs->CloseFile(fd), 0);
}

TEST(BuiltinFileTest, TheDirectoryOfABuiltinAndThoseAboveItCannotBeRemoved) {
    auto fs = InMemoryFileSystem::Create();
    ASSERT_EQ(fs->CreateDirectory("/usr", kDirMode), 0);
    ASSERT_EQ(fs->CreateDirectory("/usr/bin", kDirMode), 0);
    ASSERT_EQ(fs->AddBuiltinCommand("/usr/bin/ls", "ls"), 0);

    // /usr/bin holds nothing real, so without the builtin it would go.
    EXPECT_NE(fs->RemoveDirectory("/usr/bin"), 0);
    EXPECT_NE(fs->RemoveDirectory("/usr"), 0);

    // Once the builtin is removed, the directory is an ordinary empty one.
    EXPECT_EQ(fs->RemoveBuiltinCommand("/usr/bin/ls"), 0);
    EXPECT_EQ(fs->RemoveDirectory("/usr/bin"), 0);
    EXPECT_FALSE(fs->IsBuiltinCommand("/usr/bin/ls").has_value());
}

TEST(BuiltinFileTest, AddingTwiceOrAtTheRootFails) {
    auto fs = WithEchoInBin();
    EXPECT_NE(fs->AddBuiltinCommand("/bin/echo", "cat"), 0);
    EXPECT_NE(fs->AddBuiltinCommand("/", "cat"), 0);
    EXPECT_NE(fs->RemoveBuiltinCommand("/bin/cat"), 0);
}

TEST(BuiltinFileTest, AReadOnlyViewSeesTheBuiltinsOfWhatItWraps) {
    auto inner = WithEchoInBin();
    auto view = ReadOnlyFileSystem::Create(inner);
    EXPECT_EQ(view->IsBuiltinCommand("/bin/echo").value_or(""), "echo");
    EXPECT_TRUE(Lists(*view, "/bin", "echo", DirectoryEntryType::File));
    EXPECT_EQ(ReadAll(*view, "/bin/echo"), BuiltinCommandFileContent("echo"));
    // But it is the inner filesystem's builtin, not the view's to remove.
    EXPECT_NE(view->RemoveBuiltinCommand("/bin/echo"), 0);
}

TEST(BuiltinFileTest, ASubViewSeesTheBuiltinsUnderItsBase) {
    auto inner = WithEchoInBin();
    auto view = SubFileSystem::Create(inner, "/bin");
    EXPECT_EQ(view->IsBuiltinCommand("/echo").value_or(""), "echo");
    EXPECT_EQ(ReadAll(*view, "/echo"), BuiltinCommandFileContent("echo"));
    // Protection is the inner filesystem's, and holds through the view.
    EXPECT_NE(view->RemoveFile("/echo"), 0);
    EXPECT_LT(view->OpenFile("/echo", kFileOpenWriteCreateTruncate, kFileCreateMode), 0);
}

TEST(BuiltinFileTest, AComposedFilesystemSeesBothItsMainsAndItsMountsBuiltins) {
    auto main = WithEchoInBin();
    auto tools = InMemoryFileSystem::Create();
    ASSERT_EQ(tools->AddBuiltinCommand("/ls", "ls"), 0);
    auto composed = ComposedFileSystem::Create(main, "/tools", tools);

    EXPECT_EQ(composed->IsBuiltinCommand("/bin/echo").value_or(""), "echo");
    EXPECT_EQ(composed->IsBuiltinCommand("/tools/ls").value_or(""), "ls");
    EXPECT_TRUE(Lists(*composed, "/tools", "ls", DirectoryEntryType::File));
    EXPECT_NE(composed->RemoveDirectory("/bin"), 0);
}

TEST(BuiltinFileTest, AMountHidesTheBuiltinsBeneathIt) {
    // A mount overrides everything underneath it -- including a builtin of the
    // filesystem this one is a view of.
    auto view = ReadOnlyFileSystem::Create(WithEchoInBin());
    view->Mount("/bin", InMemoryFileSystem::Create());
    EXPECT_FALSE(view->IsBuiltinCommand("/bin/echo").has_value());
    EXPECT_FALSE(Lists(*view, "/bin", "echo", DirectoryEntryType::File));
}

TEST(BuiltinFileTest, AFilesystemsOwnBuiltinComesBeforeAnythingUnderneath) {
    // Placed on the host over a mounted directory: the host's own list is
    // asked first, and the directory -- served by the mount, which knows
    // nothing of the builtin -- is still pinned.
    auto host = InMemoryFileSystem::Create();
    auto mounted = InMemoryFileSystem::Create();
    ASSERT_EQ(mounted->AddBuiltinCommand("/ls", "ls"), 0);
    host->Mount("/bin", mounted);
    ASSERT_EQ(host->AddBuiltinCommand("/bin/ls", "echo"), 0);

    EXPECT_EQ(host->IsBuiltinCommand("/bin/ls").value_or(""), "echo");
    EXPECT_EQ(ReadAll(*host, "/bin/ls"), BuiltinCommandFileContent("echo"));
    EXPECT_NE(host->RemoveDirectory("/bin"), 0);
}

TEST(BuiltinFileTest, APhysicalDirectoryHoldingOnlyABuiltinIsStillPinned) {
    const std::string root = (std::filesystem::temp_directory_path() / "haisos_builtin_file_test").u8string();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root + "/bin");
    auto fs = PhysicalFileSystem::Create(root);
    ASSERT_EQ(fs->AddBuiltinCommand("/bin/pwd", "pwd"), 0);

    // Nothing is written to disk: the builtin exists only on the filesystem.
    EXPECT_FALSE(std::filesystem::exists(root + "/bin/pwd"));
    EXPECT_TRUE(Lists(*fs, "/bin", "pwd", DirectoryEntryType::File));
    EXPECT_EQ(ReadAll(*fs, "/bin/pwd"), BuiltinCommandFileContent("pwd"));
    EXPECT_NE(fs->RemoveDirectory("/bin"), 0);
    EXPECT_TRUE(std::filesystem::exists(root + "/bin"));
    std::filesystem::remove_all(root);
}

TEST(BuiltinFileTest, DescriptorsOfStackedFilesystemsNeverCollide) {
    // A read-only view of a filesystem with a mount of its own, itself given a
    // mount: descriptors from the inner and the outer mount tables must not
    // collide, or the outer would claim the inner's descriptor as its own.
    auto inner = InMemoryFileSystem::Create();
    auto innerMounted = InMemoryFileSystem::Create();
    int fd = innerMounted->OpenFile("/a.txt", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_GE(fd, 0);
    innerMounted->WriteFile(fd, "inner", 5);
    innerMounted->CloseFile(fd);
    inner->Mount("/in", innerMounted);

    auto outer = ReadOnlyFileSystem::Create(inner);
    auto outerMounted = InMemoryFileSystem::Create();
    fd = outerMounted->OpenFile("/b.txt", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_GE(fd, 0);
    outerMounted->WriteFile(fd, "outer", 5);
    outerMounted->CloseFile(fd);
    outer->Mount("/out", outerMounted);

    const int innerFd = outer->OpenFile("/in/a.txt", kFileOpenReadOnly);
    const int outerFd = outer->OpenFile("/out/b.txt", kFileOpenReadOnly);
    ASSERT_GE(innerFd, 0);
    ASSERT_GE(outerFd, 0);
    EXPECT_NE(innerFd, outerFd);

    char buffer[8] = {};
    ASSERT_EQ(outer->ReadFile(innerFd, buffer, sizeof(buffer)), 5);
    EXPECT_EQ(std::string(buffer, 5), "inner");
    ASSERT_EQ(outer->ReadFile(outerFd, buffer, sizeof(buffer)), 5);
    EXPECT_EQ(std::string(buffer, 5), "outer");
    outer->CloseFile(innerFd);
    outer->CloseFile(outerFd);
}
