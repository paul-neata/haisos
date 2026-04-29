#include <gtest/gtest.h>
#include "Factory.h"
#include "Console.h"
#include "ToolFactory.h"
#include "tests/mocks/MockHTTPClient.h"

using namespace Haisos;
using namespace Haisos::Mocks;

TEST(FactoryTest, CreateConsole) {
    Factory factory;
    auto console = factory.CreateConsole(false);
    EXPECT_NE(console, nullptr);

    auto consoleWithLog = factory.CreateConsole(true);
    EXPECT_NE(consoleWithLog, nullptr);
}

TEST(FactoryTest, CreateHTTPClient) {
    Factory factory;
    auto httpClient = factory.CreateHTTPClient();
    EXPECT_NE(httpClient, nullptr);
}

TEST(FactoryTest, CreateLLMCommunicator) {
    Factory factory;
    auto httpClient = std::make_unique<MockHTTPClient>();
    auto llmCommunicator = factory.CreateLLMCommunicator(
        std::move(httpClient),
        "http://localhost:11434/api/chat",
        "llama3",
        "");
    EXPECT_NE(llmCommunicator, nullptr);
}

TEST(FactoryTest, CreateToolFactory) {
    Factory factory;
    auto toolFactory = factory.CreateToolFactory(factory);
    EXPECT_NE(toolFactory, nullptr);
}

TEST(FactoryTest, CreateAgent) {
    Factory factory;
    auto console = factory.CreateConsole(false);
    auto httpClient = factory.CreateHTTPClient();
    auto toolFactory = factory.CreateToolFactory(factory);
    auto llmCommunicator = factory.CreateLLMCommunicator(
        std::move(httpClient),
        "http://localhost:11434/api/chat",
        "llama3",
        "");

    auto agent = factory.CreateAgent(
        std::move(llmCommunicator),
        std::move(toolFactory),
        std::move(console),
        {"You are a helpful AI assistant."},
        "test_agent",
        nullptr);

    EXPECT_NE(agent, nullptr);
    EXPECT_EQ(agent->Name(), "test_agent");
}

TEST(FactoryTest, CreateHaisosEngine) {
    Factory factory;
    auto engine = factory.CreateHaisosEngine(factory);
    EXPECT_NE(engine, nullptr);
}
