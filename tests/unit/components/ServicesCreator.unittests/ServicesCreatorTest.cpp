#include <gtest/gtest.h>
#include "ServicesCreator.h"
#include "Factory.h"

using namespace Haisos;

TEST(ServicesCreatorTest, CreateFileSystemServiceWrapsGivenFilesystem) {
    Factory factory;
    auto servicesCreator = CreateServicesCreator(factory);

    auto filesystemService = servicesCreator->CreateFileSystemService(factory.CreateFilesystem());
    EXPECT_NE(filesystemService, nullptr);
    // GetFileSystem() should return a usable reference, not crash.
    (void)filesystemService->GetFileSystem();
}

TEST(ServicesCreatorTest, CreateNetworkServiceCreatesHTTPClient) {
    Factory factory;
    auto servicesCreator = CreateServicesCreator(factory);

    auto networkService = servicesCreator->CreateNetworkService();
    ASSERT_NE(networkService, nullptr);

    auto httpClient = networkService->CreateHTTPClient();
    EXPECT_NE(httpClient, nullptr);
}

TEST(ServicesCreatorTest, CreateLLMServiceCreatesAgent) {
    Factory factory;
    auto servicesCreator = CreateServicesCreator(factory);
    auto networkService = servicesCreator->CreateNetworkService();

    auto llmService = servicesCreator->CreateLLMService(*networkService, "http://localhost:11434/api/chat", "llama3", "");
    ASSERT_NE(llmService, nullptr);

    auto agent = llmService->CreateAgent(
        {"You are a helpful AI assistant."},
        "test_agent",
        nullptr,
        factory.CreateAgentConsole());

    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->Name(), "test_agent");

    agent->Stop(0);
    agent->WaitToFinish();
}

TEST(ServicesCreatorTest, LLMServiceExposesToolFactory) {
    Factory factory;
    auto servicesCreator = CreateServicesCreator(factory);
    auto networkService = servicesCreator->CreateNetworkService();

    auto llmService = servicesCreator->CreateLLMService(*networkService, "http://localhost:11434/api/chat", "llama3", "");

    auto& toolFactory = llmService->GetToolFactory();
    EXPECT_FALSE(toolFactory.GetAvailableTools().empty());
}
