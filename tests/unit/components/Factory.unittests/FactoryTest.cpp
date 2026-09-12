#include <gtest/gtest.h>
#include <filesystem>
#include "Factory.h"

using namespace Haisos;

TEST(FactoryTest, CreatePhysicalConsole) {
    Factory factory;
    auto console = factory.CreatePhysicalConsole(false);
    EXPECT_NE(console, nullptr);

    auto consoleWithLog = factory.CreatePhysicalConsole(true);
    EXPECT_NE(consoleWithLog, nullptr);
}

TEST(FactoryTest, CreateHTTPClient) {
    Factory factory;
    auto httpClient = factory.CreateHTTPClient();
    EXPECT_NE(httpClient, nullptr);
}

TEST(FactoryTest, CreateFilesystem) {
    Factory factory;
    auto filesystem = factory.CreateFilesystem();
    EXPECT_NE(filesystem, nullptr);
}

TEST(FactoryTest, CreatePhysicalFileSystem) {
    Factory factory;
    auto filesystem = factory.CreatePhysicalFileSystem(std::filesystem::temp_directory_path().string());
    EXPECT_NE(filesystem, nullptr);
}
