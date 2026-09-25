#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "src/haisos/HaisosFileSystemBuilder.h"
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

const std::string kTestDir = "/tmp/haisos_fsbuilder_test_root";

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

TEST_F(HaisosFileSystemBuilderTest, RootNotMatchingAnyDeclaredNameFallsBackToLegacyPath) {
    auto servicesCreator = CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    HaisosFileConfig config;
    HaisosFileFsStep mem;
    mem.declare.name = "scratch";
    mem.declare.type = "MEM";
    config.fsSteps.push_back(mem);

    // "sub" isn't a declared FS name, so this should fall back to being a
    // plain directory path (legacy shorthand), resolved against haisosFileDir.
    config.rootPath = "sub";
    std::string error;
    auto fs = BuildRootFileSystem(*factory, *filesystemService, config, kTestDir, error);

    ASSERT_NE(fs, nullptr);
    EXPECT_TRUE(FileExists(*fs, "marker.txt"));
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
