#include <gtest/gtest.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "src/haisos/HaisosFileSystemBuilder.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Factory/Factory.h"
#include "src/components/ServicesCreator/ServicesCreator.h"

using namespace Haisos;

namespace {

#ifdef _WIN32
#include <fcntl.h>
constexpr int kReadOnly = _O_RDONLY;
#else
#include <fcntl.h>
constexpr int kReadOnly = O_RDONLY;
#endif

const std::string kTestDir = (std::filesystem::temp_directory_path() / "haisos_fsbuilder_test_root").u8string();

class HaisosFileSystemBuilderTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(kTestDir);
        std::filesystem::create_directories(kTestDir + "/sub");
        std::ofstream(kTestDir + "/marker.txt") << "root";
        std::ofstream(kTestDir + "/sub/marker.txt") << "sub";
    }

    void TearDown() override {
        std::filesystem::remove_all(kTestDir);
    }

    std::shared_ptr<IFactory> factory = CreateFactory();
};

bool FileExists(IFileSystem& fs, const std::string& path) {
    int fd = fs.OpenFile(path, kReadOnly);
    if (fd < 0) {
        return false;
    }
    fs.CloseFile(fd);
    return true;
}

} // namespace

TEST_F(HaisosFileSystemBuilderTest, NoFsStepsUsesHaisosFileDirWhenRootEmpty) {
    auto servicesCreator = CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    HaisosFileConfig config;
    std::string error;
    auto fs = BuildRootFileSystem(*factory, *filesystemService, config, kTestDir, error);

    ASSERT_NE(fs, nullptr);
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(FileExists(*fs, "marker.txt"));
}

TEST_F(HaisosFileSystemBuilderTest, NoFsStepsWithLegacyRootPath) {
    auto servicesCreator = CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    HaisosFileConfig config;
    config.rootPath = "sub";
    std::string error;
    auto fs = BuildRootFileSystem(*factory, *filesystemService, config, kTestDir, error);

    ASSERT_NE(fs, nullptr);
    EXPECT_TRUE(FileExists(*fs, "marker.txt"));
    EXPECT_FALSE(FileExists(*fs, "../marker.txt"));
}

TEST_F(HaisosFileSystemBuilderTest, RootByNameSelectsDeclaredFilesystem) {
    auto servicesCreator = CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    HaisosFileConfig config;
    HaisosFileFsStep physical;
    physical.declare.name = "workspace";
    physical.declare.type = "PHYSICAL";
    physical.declare.args = {"."};
    config.fsSteps.push_back(physical);

    HaisosFileFsStep mem;
    mem.declare.name = "scratch";
    mem.declare.type = "MEM";
    config.fsSteps.push_back(mem);

    config.rootPath = "scratch";
    std::string error;
    auto fs = BuildRootFileSystem(*factory, *filesystemService, config, kTestDir, error);

    ASSERT_NE(fs, nullptr);
    EXPECT_TRUE(error.empty());
    // scratch is an empty in-mem filesystem, so the disk marker isn't visible.
    EXPECT_FALSE(FileExists(*fs, "marker.txt"));
}

TEST_F(HaisosFileSystemBuilderTest, NoRootUsesLastDeclaredFilesystem) {
    auto servicesCreator = CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    HaisosFileConfig config;
    HaisosFileFsStep physical;
    physical.declare.name = "workspace";
    physical.declare.type = "PHYSICAL";
    physical.declare.args = {"."};
    config.fsSteps.push_back(physical);

    HaisosFileFsStep mem;
    mem.declare.name = "scratch";
    mem.declare.type = "MEM";
    config.fsSteps.push_back(mem);

    std::string error;
    auto fs = BuildRootFileSystem(*factory, *filesystemService, config, kTestDir, error);

    ASSERT_NE(fs, nullptr);
    // "scratch" (the last declared FS) is empty, so the disk marker isn't visible.
    EXPECT_FALSE(FileExists(*fs, "marker.txt"));
}

TEST_F(HaisosFileSystemBuilderTest, RoReferencingUnknownFilesystemIsError) {
    auto servicesCreator = CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    HaisosFileConfig config;
    HaisosFileFsStep ro;
    ro.declare.name = "readonly";
    ro.declare.type = "RO";
    ro.declare.args = {"does_not_exist"};
    config.fsSteps.push_back(ro);

    std::string error;
    auto fs = BuildRootFileSystem(*factory, *filesystemService, config, kTestDir, error);

    EXPECT_EQ(fs, nullptr);
    EXPECT_FALSE(error.empty());
}

TEST_F(HaisosFileSystemBuilderTest, MountRetargetsNamedFilesystem) {
    auto servicesCreator = CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    HaisosFileConfig config;
    HaisosFileFsStep physical;
    physical.declare.name = "main";
    physical.declare.type = "PHYSICAL";
    physical.declare.args = {"."};
    config.fsSteps.push_back(physical);

    HaisosFileFsStep mem;
    mem.declare.name = "scratch";
    mem.declare.type = "MEM";
    config.fsSteps.push_back(mem);

    HaisosFileFsStep mountStep;
    mountStep.isMount = true;
    mountStep.mount.mainFs = "main";
    mountStep.mount.path = "/mnt";
    mountStep.mount.toBeMountedFs = "scratch";
    config.fsSteps.push_back(mountStep);

    config.rootPath = "main";
    std::string error;
    auto fs = BuildRootFileSystem(*factory, *filesystemService, config, kTestDir, error);

    ASSERT_NE(fs, nullptr);
    EXPECT_TRUE(error.empty());
    // The disk marker is still visible outside the mount point...
    EXPECT_TRUE(FileExists(*fs, "marker.txt"));
    // ...and the mount point is now backed by the (empty) in-mem filesystem:
    // nothing but its "." and "..".
    auto entries = fs->ReadDirectory("/mnt");
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].name, ".");
    EXPECT_EQ(entries[1].name, "..");
}

// Once any FS is declared, ROOT names one of them: a mistyped name used to be
// taken as a directory instead, quietly, and the OS then booted on the wrong
// files or on none.
TEST_F(HaisosFileSystemBuilderTest, RootNamingNoDeclaredFilesystemIsAnError) {
    auto servicesCreator = CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    HaisosFileConfig config;
    HaisosFileFsStep mem;
    mem.declare.name = "scratch";
    mem.declare.type = "MEM";
    config.fsSteps.push_back(mem);

    // "sub" is a directory next to the haisosfile, but no declared FS.
    config.rootPath = "sub";
    std::string error;
    auto fs = BuildRootFileSystem(*factory, *filesystemService, config, kTestDir, error);

    EXPECT_EQ(fs, nullptr);
    EXPECT_NE(error.find("ROOT sub"), std::string::npos) << error;
    EXPECT_NE(error.find("declared: scratch"), std::string::npos) << error;
}

TEST_F(HaisosFileSystemBuilderTest, DevFilesystemMountedAtDevServesDevices) {
    auto filesystemService = CreateServicesCreator()->CreateFileSystemService();
    auto parsed = ParseHaisosFile("FS rootfs PHYSICAL .\nFS devfs DEV\nMOUNT rootfs /dev devfs\nROOT rootfs\nRUN /agent.md\n", {});
    ASSERT_TRUE(parsed.error.empty()) << parsed.error;
    std::string error;
    auto fs = BuildRootFileSystem(*factory, *filesystemService, parsed.config, kTestDir, error);

    ASSERT_NE(fs, nullptr) << error;
    FileStatus status;
    ASSERT_EQ(fs->Stat("/dev/null", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::CharDevice);
    ASSERT_EQ(fs->Stat("/dev/zero", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::CharDevice);
    EXPECT_TRUE(FileExists(*fs, "marker.txt"));
    // The mount point needs no /dev on the disk, and makes none.
    EXPECT_FALSE(std::filesystem::exists(kTestDir + "/dev"));
}

TEST_F(HaisosFileSystemBuilderTest, TheInitTemplatesDevLinesWorkOnceUncommented) {
    // Most haisosfiles will get /dev by uncommenting these two lines of what
    // `haisos --init` writes, in place.
    std::istringstream lines(GetHaisosFileTemplate({"echo"}));
    std::string haisosfile;
    std::string line;
    int uncommented = 0;
    while (std::getline(lines, line)) {
        if (line == "# FS devfs DEV" || line == "# MOUNT rootfs /dev devfs") {
            line = line.substr(2);
            ++uncommented;
        }
        haisosfile += line + "\n";
    }
    ASSERT_EQ(uncommented, 2);
    auto parsed = ParseHaisosFile(haisosfile, {});
    ASSERT_TRUE(parsed.error.empty()) << parsed.error;

    auto filesystemService = CreateServicesCreator()->CreateFileSystemService();
    std::string error;
    auto fs = BuildRootFileSystem(*factory, *filesystemService, parsed.config, kTestDir, error);
    ASSERT_NE(fs, nullptr) << error;
    FileStatus status;
    ASSERT_EQ(fs->Stat("/dev/null", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::CharDevice);
}

namespace {

// Builds the root filesystem of a haisosfile (a RUN is appended), the way main
// does, with the haisosfile in kTestDir.
std::shared_ptr<IFileSystem> Build(IFactory& factory, const std::string& haisosfile, std::string& error) {
    auto parsed = ParseHaisosFile(haisosfile + "RUN /agent.md\n", {});
    if (!parsed.error.empty()) {
        error = parsed.error;
        return nullptr;
    }
    auto filesystemService = CreateServicesCreator()->CreateFileSystemService();
    return BuildRootFileSystem(factory, *filesystemService, parsed.config, kTestDir, error);
}

std::string ReadAll(IFileSystem& fs, const std::string& path) {
    std::string content;
    EXPECT_TRUE(ReadWholeFile(fs, path, content)) << path;
    return content;
}

} // namespace

// --- FS ... PHYSICAL: a directory of the full physical filesystem ---

TEST_F(HaisosFileSystemBuilderTest, APhysicalDirectoryThatIsNotThereIsAnError) {
    std::string error;
    EXPECT_EQ(Build(*factory, "FS data PHYSICAL ./missing\n", error), nullptr);
    EXPECT_NE(error.find("FS data PHYSICAL ./missing"), std::string::npos) << error;
    EXPECT_NE(error.find("is not a directory"), std::string::npos) << error;
}

TEST_F(HaisosFileSystemBuilderTest, APhysicalFileIsNoDirectory) {
    std::string error;
    EXPECT_EQ(Build(*factory, "FS data PHYSICAL marker.txt\n", error), nullptr);
    EXPECT_NE(error.find("is not a directory"), std::string::npos) << error;
}

TEST_F(HaisosFileSystemBuilderTest, ALegacyRootThatIsNotThereIsAnError) {
    std::string error;
    EXPECT_EQ(Build(*factory, "ROOT missing\n", error), nullptr);
    EXPECT_NE(error.find("ROOT missing"), std::string::npos) << error;
}

TEST_F(HaisosFileSystemBuilderTest, APhysicalDirectoryMayBeAbsoluteOrClimbAboveTheHaisosFile) {
    const std::string self = std::filesystem::u8path(kTestDir).filename().u8string();
    std::string error;
    auto absolute = Build(*factory, "FS data PHYSICAL \"" + kTestDir + "/sub\"\n", error);
    ASSERT_NE(absolute, nullptr) << error;
    EXPECT_EQ(ReadAll(*absolute, "/marker.txt"), "sub");

    auto relative = Build(*factory, "FS data PHYSICAL ../" + self + "/sub\n", error);
    ASSERT_NE(relative, nullptr) << error;
    EXPECT_EQ(ReadAll(*relative, "/marker.txt"), "sub");
}

#ifndef _WIN32
// A PHYSICAL directory is jailed there -- IFactory::CreatePhysicalFileSystem
// -- rather than made a SubFileSystem of the full filesystem, whose
// confinement goes by the path as written: a link inside the directory
// leading out of it would take a process anywhere on the disk.
TEST_F(HaisosFileSystemBuilderTest, ALinkInsideAPhysicalDirectoryCannotLeadOutOfIt) {
    std::filesystem::create_directory_symlink("..", kTestDir + "/sub/up");
    std::string error;
    auto fs = Build(*factory, "FS data PHYSICAL ./sub\n", error);
    ASSERT_NE(fs, nullptr) << error;
    EXPECT_EQ(ReadAll(*fs, "/marker.txt"), "sub");
    FileStatus status;
    EXPECT_NE(fs->Stat("/up/marker.txt", status), 0);

    // What a SubFileSystem of the full filesystem would have let through.
    auto sub = CreateServicesCreator()->CreateFileSystemService()->CreateSubFileSystem(
        factory->CreateFullPhysicalFileSystem(), kTestDir + "/sub");
    EXPECT_EQ(sub->Stat("/up/marker.txt", status), 0);
}

// On Linux the full physical filesystem is the disk from its root.
TEST_F(HaisosFileSystemBuilderTest, APhysicalSlashIsTheWholeDisk) {
    std::string error;
    auto fs = Build(*factory, "FS host PHYSICAL /\n", error);
    ASSERT_NE(fs, nullptr) << error;
    EXPECT_EQ(ReadAll(*fs, kTestDir + "/sub/marker.txt"), "sub");
}
#else
namespace {

// kTestDir as a path of the full physical filesystem: /c/Users/...
std::string FullPathOf(const std::string& hostPath) {
    const std::filesystem::path host = std::filesystem::u8path(hostPath);
    const std::string drive = host.root_name().u8string();
    return "/" + std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(drive[0])))) +
        "/" + host.relative_path().generic_u8string();
}

} // namespace

// On Windows a PHYSICAL directory may be written as a path of the full
// physical filesystem, or with its drive letter, with '\' or '/' in any mix.
TEST_F(HaisosFileSystemBuilderTest, APhysicalDirectoryMayBeWrittenInEveryWindowsForm) {
    const std::filesystem::path host = std::filesystem::u8path(kTestDir);
    ASSERT_EQ(host.root_name().u8string().size(), 2u) << "the temporary directory is on no drive: " << kTestDir;
    const std::string full = FullPathOf(kTestDir);
    std::string upperFull = full;
    upperFull[1] = static_cast<char>(std::toupper(static_cast<unsigned char>(upperFull[1])));
    const std::string forward = host.generic_u8string();
    std::string backward = forward;
    std::replace(backward.begin(), backward.end(), '/', '\\');
    std::string backwardFull = full;
    std::replace(backwardFull.begin(), backwardFull.end(), '/', '\\');

    for (const std::string& written : {full + "/sub", upperFull + "/sub", backwardFull + "\\sub",
                                       forward + "/sub", backward + "\\sub", forward + "\\sub", std::string("sub")}) {
        std::string error;
        auto fs = Build(*factory, "FS data PHYSICAL \"" + written + "\"\n", error);
        ASSERT_NE(fs, nullptr) << written << ": " << error;
        EXPECT_EQ(ReadAll(*fs, "/marker.txt"), "sub") << written;
    }
}

TEST_F(HaisosFileSystemBuilderTest, APhysicalPathOfNoDriveIsAnError) {
    std::string error;
    EXPECT_EQ(Build(*factory, "FS data PHYSICAL /tmp\n", error), nullptr);
    EXPECT_NE(error.find("no drive letter"), std::string::npos) << error;
    EXPECT_EQ(Build(*factory, "FS data PHYSICAL c:tmp\n", error), nullptr);
    EXPECT_NE(error.find("current directory"), std::string::npos) << error;
}

// "/" on Windows is the full physical filesystem itself: a directory per drive.
TEST_F(HaisosFileSystemBuilderTest, APhysicalSlashIsTheWholeMachine) {
    std::string error;
    auto fs = Build(*factory, "FS host PHYSICAL /\n", error);
    ASSERT_NE(fs, nullptr) << error;
    const std::string full = FullPathOf(kTestDir);
    bool listed = false;
    for (const auto& entry : fs->ReadDirectory("/")) {
        listed = listed || entry.name == full.substr(1, 1);
    }
    EXPECT_TRUE(listed) << full;
    EXPECT_EQ(ReadAll(*fs, full + "/sub/marker.txt"), "sub");
}
#endif
