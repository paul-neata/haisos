#include <gtest/gtest.h>
#include <filesystem>
#include "Factory.h"

using namespace Haisos;

TEST(FactoryTest, CreatePhysicalConsole) {
    auto factory = Factory::Create();
    auto console = factory->CreatePhysicalConsole();
    EXPECT_NE(console, nullptr);
}

TEST(FactoryTest, CreateServicesCreator) {
    auto factory = Factory::Create();
    auto servicesCreator = factory->CreateServicesCreator();
    EXPECT_NE(servicesCreator, nullptr);
}

TEST(FactoryTest, CreatePhysicalFileSystem) {
    auto factory = Factory::Create();
    auto filesystem = factory->CreatePhysicalFileSystem(std::filesystem::temp_directory_path().string());
    EXPECT_NE(filesystem, nullptr);
}

TEST(FactoryTest, GetNextGloballyUniquePIDNeverRepeats) {
    // The allocator is the program's, not the factory's, so even two factories
    // never hand out the same pid.
    auto factory = Factory::Create();
    auto other = Factory::Create();

    uint64_t first = factory->GetNextGloballyUniquePID();
    uint64_t second = other->GetNextGloballyUniquePID();
    uint64_t third = factory->GetNextGloballyUniquePID();

    EXPECT_NE(first, 0u);
    EXPECT_NE(first, second);
    EXPECT_NE(second, third);
    EXPECT_NE(first, third);
}
