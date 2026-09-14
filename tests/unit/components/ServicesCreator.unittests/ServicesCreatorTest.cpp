#include <gtest/gtest.h>
#include "ServicesCreator.h"
#include "Factory.h"

using namespace Haisos;

namespace {

// Portable-enough flags for exercising the filesystem composition methods
// directly (mirrors src/components/Filesystem/FilesystemUtils.h's constants).
#ifdef _WIN32
#include <fcntl.h>
constexpr int kReadOnly = _O_RDONLY;
constexpr int kWriteCreateTruncate = _O_WRONLY | _O_CREAT | _O_TRUNC;
#else
#include <fcntl.h>
constexpr int kReadOnly = O_RDONLY;
constexpr int kWriteCreateTruncate = O_WRONLY | O_CREAT | O_TRUNC;
#endif

}

TEST(ServicesCreatorTest, CreateNetworkServiceCreatesHTTPClient) {
    Factory factory;
    auto servicesCreator = CreateServicesCreator();

    auto networkService = servicesCreator->CreateNetworkService();
    ASSERT_NE(networkService, nullptr);

    auto httpClient = networkService->CreateHTTPClient();
    EXPECT_NE(httpClient, nullptr);
}

TEST(ServicesCreatorTest, CreateLLMServiceCreatesAgent) {
    Factory factory;
    auto servicesCreator = CreateServicesCreator();
    auto networkService = servicesCreator->CreateNetworkService();

    auto llmService = servicesCreator->CreateLLMService(*networkService, "http://localhost:11434/api/chat", "llama3", "");
    ASSERT_NE(llmService, nullptr);

    auto agent = llmService->CreateAgent(
        {"You are a helpful AI assistant."},
        "test_agent",
        nullptr,
        llmService->CreateAgentConsole());

    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->Name(), "test_agent");

    agent->Stop(0);
    agent->WaitToFinish();
}

TEST(ServicesCreatorTest, LLMServiceExposesToolFactory) {
    Factory factory;
    auto servicesCreator = CreateServicesCreator();
    auto networkService = servicesCreator->CreateNetworkService();

    auto llmService = servicesCreator->CreateLLMService(*networkService, "http://localhost:11434/api/chat", "llama3", "");

    auto& toolFactory = llmService->GetToolFactory();
    EXPECT_FALSE(toolFactory.GetAvailableTools().empty());
}

TEST(ServicesCreatorTest, CreateEmptyInMemFileSystemIsReadWrite) {
    Factory factory;
    auto servicesCreator = CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    auto fs = filesystemService->CreateEmptyInMemFileSystem();
    ASSERT_NE(fs, nullptr);

    int fd = fs->OpenFile("hello.txt", kWriteCreateTruncate, 0);
    ASSERT_GE(fd, 0);
    const char* data = "hi";
    EXPECT_EQ(fs->WriteFile(fd, data, 2), 2);
    fs->CloseFile(fd);

    fd = fs->OpenFile("hello.txt", kReadOnly);
    ASSERT_GE(fd, 0);
    char buf[8] = {};
    ssize_t n = fs->ReadFile(fd, buf, sizeof(buf));
    fs->CloseFile(fd);
    EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), "hi");
}

TEST(ServicesCreatorTest, CreateReadOnlyFileSystemRejectsWrites) {
    Factory factory;
    auto servicesCreator = CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    auto inner = filesystemService->CreateEmptyInMemFileSystem();
    auto readOnly = filesystemService->CreateReadOnlyFileSystem(inner);
    ASSERT_NE(readOnly, nullptr);

    EXPECT_LT(readOnly->OpenFile("new.txt", kWriteCreateTruncate, 0), 0);
    EXPECT_LT(readOnly->CreateDirectory("sub", 0), 0);
}

TEST(ServicesCreatorTest, CreateSubFileSystemConfinesToBasePath) {
    Factory factory;
    auto servicesCreator = CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    auto root = filesystemService->CreateEmptyInMemFileSystem();
    ASSERT_EQ(root->CreateDirectory("sub", 0), 0);
    int fd = root->OpenFile("sub/inner.txt", kWriteCreateTruncate, 0);
    ASSERT_GE(fd, 0);
    root->WriteFile(fd, "x", 1);
    root->CloseFile(fd);
    fd = root->OpenFile("outside.txt", kWriteCreateTruncate, 0);
    ASSERT_GE(fd, 0);
    root->WriteFile(fd, "y", 1);
    root->CloseFile(fd);

    auto sub = filesystemService->CreateSubFileSystem(root, "sub");
    ASSERT_NE(sub, nullptr);

    EXPECT_GE(sub->OpenFile("inner.txt", kReadOnly), 0);
    // "outside.txt" lives above the sub-root, so it isn't visible from here.
    EXPECT_LT(sub->OpenFile("outside.txt", kReadOnly), 0);
    // Even an explicit escape attempt stays confined.
    EXPECT_LT(sub->OpenFile("../outside.txt", kReadOnly), 0);
}

TEST(ServicesCreatorTest, MountFileSystemOverlaysAtMountPoint) {
    Factory factory;
    auto servicesCreator = CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    auto main = filesystemService->CreateEmptyInMemFileSystem();
    int fd = main->OpenFile("main.txt", kWriteCreateTruncate, 0);
    ASSERT_GE(fd, 0);
    main->WriteFile(fd, "m", 1);
    main->CloseFile(fd);

    auto mounted = filesystemService->CreateEmptyInMemFileSystem();
    fd = mounted->OpenFile("mounted.txt", kWriteCreateTruncate, 0);
    ASSERT_GE(fd, 0);
    mounted->WriteFile(fd, "n", 1);
    mounted->CloseFile(fd);

    auto composed = filesystemService->MountFileSystem(main, "/data", mounted);
    ASSERT_NE(composed, nullptr);

    // Files under the mount point come from the mounted filesystem.
    EXPECT_GE(composed->OpenFile("/data/mounted.txt", kReadOnly), 0);
    // Files elsewhere still come from main.
    EXPECT_GE(composed->OpenFile("/main.txt", kReadOnly), 0);
    // The mount point is synthesized as a directory even though main never had one.
    auto rootEntries = composed->ReadDirectory("/");
    bool foundData = false;
    for (const auto& entry : rootEntries) {
        if (entry.name == "data") {
            foundData = true;
        }
    }
    EXPECT_TRUE(foundData);
}
