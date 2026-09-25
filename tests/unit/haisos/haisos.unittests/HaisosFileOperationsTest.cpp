#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
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
        return ApplyHaisosFileOperations(*factory, *root, parsed.config.setupOperations, kHostDir, error) &&
            ApplyHaisosFileOperations(*factory, *root, parsed.config.outCopyOperations, kHostDir, error);
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
