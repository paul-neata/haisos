#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include "src/haisos/HaisosFileOperations.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Factory/Factory.h"
#include "src/components/ServicesCreator/ServicesCreator.h"

using namespace Haisos;

namespace {

const std::string kHostDir = "/tmp/haisos_fileops_test_host";

class HaisosFileOperationsTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(kHostDir);
        std::filesystem::create_directories(kHostDir + "/in");
        std::ofstream(kHostDir + "/in/source.txt") << "from the host";
        root = CreateServicesCreator()->CreateFileSystemService()->CreateEmptyInMemFileSystem();
    }

    void TearDown() override {
        std::filesystem::remove_all(kHostDir);
    }

    // Parses a haisosfile (with a RUN appended) and applies its setup and
    // out-copy operations to root, the way main does.
    bool Apply(const std::string& directives, std::string& error) {
        auto parsed = ParseHaisosFile(directives + "RUN /agent.md\n", {});
        if (!parsed.error.empty()) {
            error = parsed.error;
            return false;
        }
        // The root is declared as "rootfs", as in the --init template, so a
        // BUILTIN can name it; "other" is a second FS it can name too.
        namedFileSystems = {{"rootfs", root}, {"other", other}};
        HaisosFileBuiltinTargets builtins;
        builtins.namedFileSystems = &namedFileSystems;
        builtins.builtinCommands = builtinCommands.get();
        builtins.configurator = configurator.get();
        return ApplyHaisosFileOperations(*factory, *root, parsed.config.setupOperations, kHostDir, error, builtins) &&
            ApplyHaisosFileOperations(*factory, *root, parsed.config.outCopyOperations, kHostDir, error, builtins);
    }

    std::string Read(const std::string& path) {
        std::string content;
        EXPECT_TRUE(ReadWholeFile(*root, path, content)) << path;
        return content;
    }

    bool Exists(const std::string& path) {
        std::string content;
        return ReadWholeFile(*root, path, content);
    }

    static std::string ReadHost(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    std::shared_ptr<IFactory> factory = CreateFactory();
    std::shared_ptr<IFileSystem> root;
    std::shared_ptr<IFileSystem> other = CreateServicesCreator()->CreateFileSystemService()->CreateEmptyInMemFileSystem();
    std::shared_ptr<IBuiltinCommands> builtinCommands = factory->CreateBuiltinCommands();
    std::shared_ptr<IBuiltinConfigurator> configurator = factory->CreateBuiltinConfigurator();
    std::unordered_map<std::string, std::shared_ptr<IFileSystem>> namedFileSystems;
};

} // namespace

TEST_F(HaisosFileOperationsTest, CreateWritesAFileAndItsParentDirectories) {
    std::string error;
    ASSERT_TRUE(Apply("CREATE /a/b/c.txt 'hello'\n", error)) << error;
    EXPECT_EQ(Read("/a/b/c.txt"), "hello");
}

TEST_F(HaisosFileOperationsTest, CreateReplacesAndAppendAppends) {
    std::string error;
    ASSERT_TRUE(Apply(
        "CREATE /f.txt 'first'\n"
        "CREATE /f.txt 'second'\n"
        "APPEND /f.txt text: and more\n"
        "APPEND /new.txt 'made by append'\n", error)) << error;
    EXPECT_EQ(Read("/f.txt"), "second and more");
    EXPECT_EQ(Read("/new.txt"), "made by append");
}

TEST_F(HaisosFileOperationsTest, CreateMultilineContentLandsVerbatim) {
    std::string error;
    ASSERT_TRUE(Apply("CREATE /m.md multiline END\n# title\nbody #\n\nEND\n", error)) << error;
    EXPECT_EQ(Read("/m.md"), "# title\nbody #\n");
}

TEST_F(HaisosFileOperationsTest, CreateOverADirectoryFails) {
    std::string error;
    ASSERT_TRUE(Apply("CREATE /dir/x 'x'\n", error)) << error;
    EXPECT_FALSE(Apply("CREATE /dir 'x'\n", error));
    EXPECT_NE(error.find("line 1"), std::string::npos) << error;
}

TEST_F(HaisosFileOperationsTest, CreateThroughAFileFails) {
    std::string error;
    ASSERT_TRUE(Apply("CREATE /file 'x'\n", error)) << error;
    EXPECT_FALSE(Apply("CREATE /file/child 'x'\n", error));
}

TEST_F(HaisosFileOperationsTest, CreateOnAReadOnlyRootFails) {
    root = CreateServicesCreator()->CreateFileSystemService()->CreateReadOnlyFileSystem(root);
    std::string error;
    EXPECT_FALSE(Apply("CREATE /x 'x'\n", error));
    EXPECT_FALSE(error.empty());
}

TEST_F(HaisosFileOperationsTest, DeleteRemovesAFile) {
    std::string error;
    ASSERT_TRUE(Apply("CREATE /keep 'k'\nCREATE /gone 'g'\nDELETE /gone\n", error)) << error;
    EXPECT_FALSE(Exists("/gone"));
    EXPECT_TRUE(Exists("/keep"));
}

TEST_F(HaisosFileOperationsTest, DeleteRemovesADirectoryAndEverythingInIt) {
    std::string error;
    ASSERT_TRUE(Apply("CREATE /d/a.txt 'a'\nCREATE /d/sub/b.txt 'b'\nDELETE /d\n", error)) << error;
    for (const auto& entry : root->ReadDirectory("/")) {
        EXPECT_NE(entry.name, "d");
    }
}

TEST_F(HaisosFileOperationsTest, DeleteOfAMissingPathFails) {
    std::string error;
    EXPECT_FALSE(Apply("DELETE /nothing/here\n", error));
    EXPECT_NE(error.find("does not exist"), std::string::npos) << error;
}

TEST_F(HaisosFileOperationsTest, DeleteOfTheRootFails) {
    std::string error;
    EXPECT_FALSE(Apply("DELETE /\n", error));
}

TEST_F(HaisosFileOperationsTest, CopyBringsAHostFileIn) {
    std::string error;
    ASSERT_TRUE(Apply("COPY in/source.txt /work/copied.txt\n", error)) << error;
    EXPECT_EQ(Read("/work/copied.txt"), "from the host");
}

TEST_F(HaisosFileOperationsTest, CopyAcceptsAnAbsoluteHostPath) {
    std::string error;
    ASSERT_TRUE(Apply("COPY " + kHostDir + "/in/source.txt /copied.txt\n", error)) << error;
    EXPECT_EQ(Read("/copied.txt"), "from the host");
}

TEST_F(HaisosFileOperationsTest, CopyOfAMissingOrDirectoryHostPathFails) {
    std::string error;
    EXPECT_FALSE(Apply("COPY in/missing.txt /x\n", error));
    EXPECT_FALSE(Apply("COPY in /x\n", error));
}

TEST_F(HaisosFileOperationsTest, CopyIsNotCappedInSize) {
    // Larger than ReadWholeFile's 10 MB cap, which would truncate silently.
    const std::string big(11 * 1024 * 1024, 'x');
    std::ofstream(kHostDir + "/in/big.bin", std::ios::binary) << big;
    std::string error;
    ASSERT_TRUE(Apply("COPY in/big.bin /big.bin\nOUTCOPY /big.bin out/big.bin\n", error)) << error;
    EXPECT_EQ(ReadHost(kHostDir + "/out/big.bin").size(), big.size());
}

TEST_F(HaisosFileOperationsTest, OutCopyPullsAFileOutCreatingHostDirectories) {
    std::string error;
    ASSERT_TRUE(Apply("CREATE /result.txt 'done'\nOUTCOPY /result.txt out/deep/result.txt\n", error)) << error;
    EXPECT_EQ(ReadHost(kHostDir + "/out/deep/result.txt"), "done");
}

TEST_F(HaisosFileOperationsTest, OutCopyOfAMissingFileFails) {
    std::string error;
    EXPECT_FALSE(Apply("OUTCOPY /missing.txt out/missing.txt\n", error));
    EXPECT_NE(error.find("OUTCOPY"), std::string::npos) << error;
}

// --- CREATE_DIR ---

TEST_F(HaisosFileOperationsTest, CreateDirMakesADirectoryAndItsParents) {
    std::string error;
    ASSERT_TRUE(Apply("CREATE_DIR /usr/local/bin\n", error)) << error;
    EXPECT_EQ(EntryTypeOf(*root, "/usr/local/bin"), std::optional<char>(DirectoryEntryType::Dir));
    EXPECT_EQ(EntryTypeOf(*root, "/usr"), std::optional<char>(DirectoryEntryType::Dir));
}

TEST_F(HaisosFileOperationsTest, CreateDirOfAnExistingDirectoryIsFine) {
    std::string error;
    ASSERT_TRUE(Apply("CREATE /bin/keep.txt 'k'\nCREATE_DIR /bin\nCREATE_DIR /\n", error)) << error;
    EXPECT_EQ(Read("/bin/keep.txt"), "k");
}

TEST_F(HaisosFileOperationsTest, CreateDirOverAFileFails) {
    std::string error;
    EXPECT_FALSE(Apply("CREATE /bin 'not a dir'\nCREATE_DIR /bin\n", error));
    EXPECT_NE(error.find("line 2"), std::string::npos) << error;
}

// --- BUILTIN ---

TEST_F(HaisosFileOperationsTest, BuiltinPlacesABuiltinAtEveryPathGiven) {
    std::string error;
    ASSERT_TRUE(Apply("CREATE_DIR /bin\nCREATE_DIR /usr/bin\nBUILTIN rootfs ls /bin/ls /usr/bin/ls\n", error)) << error;
    EXPECT_EQ(root->IsBuiltinCommand("/bin/ls").value_or(""), "ls");
    EXPECT_EQ(root->IsBuiltinCommand("/usr/bin/ls").value_or(""), "ls");
}

TEST_F(HaisosFileOperationsTest, BuiltinGoesOnTheFilesystemItNames) {
    std::string error;
    ASSERT_TRUE(Apply("BUILTIN other echo /echo\n", error)) << error;
    EXPECT_EQ(other->IsBuiltinCommand("/echo").value_or(""), "echo");
    EXPECT_FALSE(root->IsBuiltinCommand("/echo").has_value());
}

TEST_F(HaisosFileOperationsTest, BuiltinNeedsItsDirectoryToExist) {
    std::string error;
    EXPECT_FALSE(Apply("BUILTIN rootfs echo /bin/echo\n", error));
    EXPECT_NE(error.find("/bin does not exist"), std::string::npos) << error;
}

TEST_F(HaisosFileOperationsTest, BuiltinCannotReplaceAFile) {
    std::string error;
    EXPECT_FALSE(Apply("CREATE /bin/echo 'mine'\nBUILTIN rootfs echo /bin/echo\n", error));
    EXPECT_NE(error.find("already exists"), std::string::npos) << error;
}

TEST_F(HaisosFileOperationsTest, BuiltinOfAnUnknownNameFails) {
    std::string error;
    EXPECT_FALSE(Apply("CREATE_DIR /bin\nBUILTIN rootfs nosuch /bin/nosuch\n", error));
    EXPECT_NE(error.find("unknown builtin 'nosuch'"), std::string::npos) << error;
    EXPECT_NE(error.find("echo"), std::string::npos) << error;
}

TEST_F(HaisosFileOperationsTest, BuiltinOnAnUnknownFilesystemFails) {
    std::string error;
    EXPECT_FALSE(Apply("BUILTIN nofs echo /echo\n", error));
    EXPECT_NE(error.find("unknown filesystem 'nofs'"), std::string::npos) << error;
}

TEST_F(HaisosFileOperationsTest, DeleteCannotRemoveABuiltinOrItsDirectory) {
    std::string error;
    ASSERT_TRUE(Apply("CREATE_DIR /bin\nBUILTIN rootfs cat /bin/cat\n", error)) << error;
    EXPECT_FALSE(Apply("DELETE /bin\n", error));
    EXPECT_FALSE(Apply("DELETE /bin/cat\n", error));
    EXPECT_EQ(root->IsBuiltinCommand("/bin/cat").value_or(""), "cat");
}

TEST_F(HaisosFileOperationsTest, BuiltinWithoutTargetsFails) {
    auto parsed = ParseHaisosFile("BUILTIN rootfs echo /echo\nRUN /agent.md\n", {});
    ASSERT_TRUE(parsed.error.empty()) << parsed.error;
    std::string error;
    EXPECT_FALSE(ApplyHaisosFileOperations(*factory, *root, parsed.config.setupOperations, kHostDir, error));
    EXPECT_NE(error.find("not available"), std::string::npos) << error;
}

TEST_F(HaisosFileOperationsTest, DevNullSwallowsWritesAndDevCannotBeChanged) {
    root->Mount("/dev", CreateServicesCreator()->CreateFileSystemService()->CreateDeviceFileSystem());
    std::string error;
    ASSERT_TRUE(Apply("CREATE /dev/null 'gone'\nAPPEND /dev/null 'gone too'\n", error)) << error;
    EXPECT_EQ(Read("/dev/null"), "");

    EXPECT_FALSE(Apply("CREATE /dev/new.txt 'x'\n", error));
    EXPECT_FALSE(Apply("CREATE_DIR /dev/sub\n", error));
    EXPECT_FALSE(Apply("DELETE /dev/null\n", error));
    EXPECT_FALSE(Apply("DELETE /dev\n", error));
    EXPECT_FALSE(Apply("OUTCOPY /dev/zero ./zero\n", error));
    EXPECT_NE(error.find("device"), std::string::npos) << error;
    EXPECT_EQ(EntryTypeOf(*root, "/dev/null"), std::optional<char>(DirectoryEntryType::CharDevice));
}

TEST_F(HaisosFileOperationsTest, DeleteOfADirectoryTreeSkipsDotEntries) {
    std::string error;
    ASSERT_TRUE(Apply("CREATE /tree/a/b.txt 'b'\nCREATE /tree/c.txt 'c'\nDELETE /tree\n", error)) << error;
    EXPECT_FALSE(EntryTypeOf(*root, "/tree").has_value());
}

TEST_F(HaisosFileOperationsTest, TheInitTemplatesBuiltinsAllApplyOnceUncommented) {
    // What `haisos --init` writes lists every builtin Haisos has; uncommenting
    // CREATE_DIR /bin and every BUILTIN must place them all.
    const auto commands = builtinCommands->GetCommands();
    std::istringstream lines(GetHaisosFileTemplate(commands));
    std::string directives;
    std::string line;
    while (std::getline(lines, line)) {
        if (line == "# CREATE_DIR /bin" || line.rfind("# BUILTIN rootfs ", 0) == 0) {
            directives += line.substr(2) + "\n";
        }
    }
    std::string error;
    ASSERT_TRUE(Apply(directives, error)) << error;
    for (const auto& name : commands) {
        EXPECT_EQ(root->IsBuiltinCommand("/bin/" + name).value_or(""), name);
    }
}

#ifndef _WIN32
// DELETE behaves like `rm -rf`: a symbolic link inside the tree is removed
// itself, never descended into. Stat and ReadDirectory follow links, so a link
// to a directory looked like that directory: `DELETE /work` holding
// work/link -> ../important once emptied and removed important/, a sibling of
// the tree being deleted, and then failed on the link.
TEST_F(HaisosFileOperationsTest, DeleteRemovesALinkNotWhatItPointsAt) {
    const std::string hostRoot = kHostDir + "/root";
    std::filesystem::create_directories(hostRoot + "/important");
    std::filesystem::create_directories(hostRoot + "/work/sub");
    std::ofstream(hostRoot + "/important/keep.txt") << "precious";
    std::ofstream(hostRoot + "/work/sub/file.txt") << "doomed";
    std::filesystem::create_directory_symlink("../important", hostRoot + "/work/link");
    std::filesystem::create_symlink("../important/keep.txt", hostRoot + "/work/sub/file_link");
    std::filesystem::create_symlink(hostRoot + "/nowhere", hostRoot + "/work/dangling");
    root = factory->CreatePhysicalFileSystem(hostRoot);

    std::string error;
    ASSERT_TRUE(Apply("DELETE /work\n", error)) << error;

    EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(hostRoot + "/work")));
    EXPECT_TRUE(std::filesystem::is_directory(hostRoot + "/important"));
    EXPECT_EQ(ReadHost(hostRoot + "/important/keep.txt"), "precious");
}

// A link named by DELETE itself goes the same way: the link, not its target.
TEST_F(HaisosFileOperationsTest, DeleteOfALinkRemovesTheLinkOnly) {
    const std::string hostRoot = kHostDir + "/root";
    std::filesystem::create_directories(hostRoot + "/important");
    std::ofstream(hostRoot + "/important/keep.txt") << "precious";
    std::filesystem::create_directory_symlink("important", hostRoot + "/shortcut");
    root = factory->CreatePhysicalFileSystem(hostRoot);

    std::string error;
    ASSERT_TRUE(Apply("DELETE /shortcut\n", error)) << error;

    EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(hostRoot + "/shortcut")));
    EXPECT_EQ(ReadHost(hostRoot + "/important/keep.txt"), "precious");
}
#endif
