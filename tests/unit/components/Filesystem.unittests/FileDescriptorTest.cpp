#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include "src/components/Filesystem/BuiltinCommandFile.h"
#include "src/components/Filesystem/ComposedFileSystem.h"
#include "src/components/Filesystem/DeviceFileSystem.h"
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

void Write(IFileSystem& fs, const std::string& path, const std::string& content, int flags = kFileOpenWriteCreateTruncate) {
    auto file = fs.OpenFile(path, flags, kFileCreateMode);
    ASSERT_NE(file, nullptr) << path;
    ASSERT_EQ(file->Write(content.data(), content.size()), static_cast<ssize_t>(content.size()));
}

std::string ReadAll(IFileSystem& fs, const std::string& path) {
    std::string content;
    EXPECT_TRUE(ReadWholeFile(fs, path, content)) << path;
    return content;
}

} // namespace

TEST(FilesystemDescriptorTest, IOResultValues) {
    EXPECT_EQ(kIOError, -1);
    EXPECT_EQ(kIOBrokenPipe, -2);
    EXPECT_EQ(kIOInterrupted, -3);
}

TEST(FilesystemDescriptorTest, InMemoryAppendWritesAtTheEndBeforeEveryWrite) {
    auto fs = InMemoryFileSystem::Create();
    // Both opened before any write, as two ">> log" writers would be.
    auto a = fs->OpenFile("/log", kFileOpenWriteCreateAppend);
    auto b = fs->OpenFile("/log", kFileOpenWriteCreateAppend);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_EQ(a->Write("a", 1), 1);
    EXPECT_EQ(b->Write("b", 1), 1);
    EXPECT_EQ(a->Write("c", 1), 1);
    // Before the fix A's second write went back to its old position, giving "cb".
    EXPECT_EQ(ReadAll(*fs, "/log"), "abc");
}

TEST(FilesystemDescriptorTest, InMemoryWritesWithoutAppendStayAtTheirPosition) {
    auto fs = InMemoryFileSystem::Create();
    Write(*fs, "/f", "12345");

    auto file = fs->OpenFile("/f", kFileWriteOnlyBit);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->Write("ab", 2), 2);
    EXPECT_EQ(ReadAll(*fs, "/f"), "ab345");
}

TEST(FilesystemDescriptorTest, AReadOnlyDescriptorRefusesWritesAndAWriteOnlyOneRefusesReads) {
    auto fs = InMemoryFileSystem::Create();
    Write(*fs, "/f", "data");

    auto readOnly = fs->OpenFile("/f", kFileOpenReadOnly);
    ASSERT_NE(readOnly, nullptr);
    EXPECT_EQ(readOnly->Write("x", 1), kIOError);
    EXPECT_EQ(ReadAll(*fs, "/f"), "data");

    auto writeOnly = fs->OpenFile("/f", kFileWriteOnlyBit);
    ASSERT_NE(writeOnly, nullptr);
    char c;
    EXPECT_EQ(writeOnly->Read(&c, 1), kIOError);

    auto devices = DeviceFileSystem::Create();
    auto nullIn = devices->OpenFile("/null", kFileOpenReadOnly);
    ASSERT_NE(nullIn, nullptr);
    EXPECT_EQ(nullIn->Write("x", 1), kIOError);
    auto nullOut = devices->OpenFile("/null", kFileWriteOnlyBit);
    ASSERT_NE(nullOut, nullptr);
    EXPECT_EQ(nullOut->Read(&c, 1), kIOError);
    auto zeroIn = devices->OpenFile("/zero", kFileOpenReadOnly);
    ASSERT_NE(zeroIn, nullptr);
    EXPECT_EQ(zeroIn->Write("x", 1), kIOError);
    auto zeroOut = devices->OpenFile("/zero", kFileWriteOnlyBit);
    ASSERT_NE(zeroOut, nullptr);
    EXPECT_EQ(zeroOut->Read(&c, 1), kIOError);
}

TEST(FilesystemDescriptorTest, ADescriptorWorksUntilItsLastHolderReleasesIt) {
    auto fs = InMemoryFileSystem::Create();
    auto first = fs->OpenFile("/f", kFileWriteOnlyBit | kFileCreateBit, kFileCreateMode);
    ASSERT_NE(first, nullptr);
    auto copy = first;
    first.reset();

    EXPECT_EQ(copy->Write("hi", 2), 2);
    copy.reset();
    EXPECT_EQ(ReadAll(*fs, "/f"), "hi");
}

TEST(FilesystemDescriptorTest, ADescriptorOutlivesItsFilesystem) {
    std::shared_ptr<IFileDescriptor> writer;
    std::shared_ptr<IFileDescriptor> reader;
    {
        auto fs = InMemoryFileSystem::Create();
        writer = fs->OpenFile("/f", kFileWriteOnlyBit | kFileCreateBit, kFileCreateMode);
        reader = fs->OpenFile("/f", kFileOpenReadOnly);
        ASSERT_NE(writer, nullptr);
        ASSERT_NE(reader, nullptr);
        fs.reset();
    }
    // The last outside reference is gone; both descriptors keep working.
    EXPECT_EQ(writer->Write("hi", 2), 2);
    char buf[4] = {};
    EXPECT_EQ(reader->Read(buf, sizeof(buf) - 1), 2);
    EXPECT_EQ(std::string(buf, 2), "hi");
}

TEST(FilesystemDescriptorTest, AMountedFilesDescriptorKeepsWorkingAfterUnmount) {
    auto root = InMemoryFileSystem::Create();
    auto m = InMemoryFileSystem::Create();
    root->Mount("/m", m);

    auto file = root->OpenFile("/m/x", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(file, nullptr);
    root->Unmount("/m");

    EXPECT_EQ(file->Write("kept", 4), 4);
    file.reset();
    EXPECT_EQ(ReadAll(*m, "/x"), "kept");
}

TEST(FilesystemDescriptorTest, WrappersPassTheInnerDescriptorUp) {
    auto inner = InMemoryFileSystem::Create();
    ASSERT_EQ(inner->CreateDirectory("/d", kDirMode), 0);
    Write(*inner, "/d/f", "x");

    auto sub = SubFileSystem::Create(inner, "/d");
    auto subFile = sub->OpenFile("/f", kFileOpenWriteCreateAppend, kFileCreateMode);
    ASSERT_NE(subFile, nullptr);
    EXPECT_EQ(subFile->Write("s", 1), 1);
    subFile.reset();
    EXPECT_EQ(ReadAll(*inner, "/d/f"), "xs");

    auto overlay = InMemoryFileSystem::Create();
    auto composed = ComposedFileSystem::Create(inner, "/c", overlay);
    auto composedFile = composed->OpenFile("/c/g", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(composedFile, nullptr);
    EXPECT_EQ(composedFile->Write("o", 1), 1);
    composedFile.reset();
    EXPECT_EQ(ReadAll(*overlay, "/g"), "o");

    auto readOnly = ReadOnlyFileSystem::Create(inner);
    auto read = readOnly->OpenFile("/d/f", kFileOpenReadOnly);
    ASSERT_NE(read, nullptr);
    EXPECT_EQ(readOnly->OpenFile("/d/f", kFileWriteOnlyBit), nullptr);
    EXPECT_EQ(readOnly->OpenFile("/d/f", kFileOpenWriteCreateTruncate, kFileCreateMode), nullptr);
}

TEST(FilesystemDescriptorTest, NoFilesystemDescriptorIsATerminal) {
    auto mem = InMemoryFileSystem::Create();
    auto file = mem->OpenFile("/f", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(file, nullptr);
    EXPECT_FALSE(file->IsTerminal());

    auto devices = DeviceFileSystem::Create();
    auto null = devices->OpenFile("/null", kFileOpenReadOnly);
    ASSERT_NE(null, nullptr);
    EXPECT_FALSE(null->IsTerminal());

    ASSERT_EQ(mem->CreateDirectory("/bin", kDirMode), 0);
    ASSERT_EQ(mem->AddBuiltinCommand("/bin/echo", "echo"), 0);
    auto builtin = mem->OpenFile("/bin/echo", kFileOpenReadOnly);
    ASSERT_NE(builtin, nullptr);
    EXPECT_FALSE(builtin->IsTerminal());

    const std::string dir = (std::filesystem::temp_directory_path() / "haisos_fd_tty_test").u8string();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto physical = PhysicalFileSystem::Create(dir);
    auto hostFile = physical->OpenFile("/f", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(hostFile, nullptr);
    EXPECT_FALSE(hostFile->IsTerminal());
    hostFile.reset();
    std::filesystem::remove_all(dir);
}

#ifdef __linux__
TEST(FilesystemDescriptorTest, ReleasingAPhysicalDescriptorClosesTheHostFile) {
    const std::string dir = (std::filesystem::temp_directory_path() / "haisos_fd_close_test").u8string();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto fs = PhysicalFileSystem::Create(dir);

    const auto countOpen = [] {
        size_t count = 0;
        for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
            (void)entry;
            ++count;
        }
        return count;
    };
    const size_t before = countOpen();
    auto file = fs->OpenFile("/f", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(countOpen(), before + 1);
    file.reset();
    EXPECT_EQ(countOpen(), before);
    std::filesystem::remove_all(dir);
}
#endif

TEST(FilesystemDescriptorTest, TwoDescriptorsOfABuiltinReadIndependently) {
    auto fs = InMemoryFileSystem::Create();
    ASSERT_EQ(fs->CreateDirectory("/bin", kDirMode), 0);
    ASSERT_EQ(fs->AddBuiltinCommand("/bin/echo", "echo"), 0);

    // Both read the full note from the start, though the read positions are
    // the descriptors' own.
    auto first = fs->OpenFile("/bin/echo", kFileOpenReadOnly);
    auto second = fs->OpenFile("/bin/echo", kFileOpenReadOnly);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    const std::string note = BuiltinCommandFileContent("echo");
    char ch;
    ASSERT_EQ(first->Read(&ch, 1), 1);
    EXPECT_EQ(ch, note[0]);

    std::string rest;
    char buf[256];
    ssize_t n = first->Read(buf, sizeof(buf));
    ASSERT_GE(n, 0);
    rest.append(buf, static_cast<size_t>(n));
    EXPECT_EQ(note[0] + rest, note);

    std::string whole;
    while ((n = second->Read(buf, sizeof(buf))) > 0) {
        whole.append(buf, static_cast<size_t>(n));
    }
    EXPECT_EQ(whole, note);

    EXPECT_EQ(first->Write("x", 1), kIOError);
    EXPECT_EQ(second->Write("x", 1), kIOError);
}
