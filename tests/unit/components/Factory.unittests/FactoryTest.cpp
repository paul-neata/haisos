#include <gtest/gtest.h>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include "Factory.h"
#include "src/components/Filesystem/FilesystemUtils.h"

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

namespace {

// The system's temporary directory, as a path of the full physical filesystem:
// the path itself on Linux, /c/Users/.../Temp on Windows.
std::string TemporaryDirectoryInTheFullFileSystem() {
    std::filesystem::path temp = std::filesystem::temp_directory_path();
    if (!temp.has_filename()) {
        temp = temp.parent_path();
    }
#ifdef _WIN32
    const std::string drive = temp.root_name().u8string();
    return "/" + std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(drive[0])))) +
        "/" + temp.relative_path().generic_u8string();
#else
    return temp.u8string();
#endif
}

} // namespace

TEST(FactoryTest, CreateFullPhysicalFileSystemHoldsTheWholeDisk) {
    auto factory = Factory::Create();
    auto full = factory->CreateFullPhysicalFileSystem();
    ASSERT_NE(full, nullptr);
    FileStatus status;
    ASSERT_EQ(full->Stat("/", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
    const std::string temp = TemporaryDirectoryInTheFullFileSystem();
    ASSERT_EQ(full->Stat(temp, status), 0) << temp;
    EXPECT_EQ(status.type, DirectoryEntryType::Dir);
}

// A directory of the full physical filesystem, named as it is there, is what
// CreatePhysicalFileSystem roots a filesystem at.
TEST(FactoryTest, CreatePhysicalFileSystemTakesADirectoryOfTheFullOne) {
    auto factory = Factory::Create();
    const std::string temp = TemporaryDirectoryInTheFullFileSystem();
    const std::string name = "haisos_factory_full_path_test.txt";
    const std::filesystem::path host = std::filesystem::temp_directory_path() / name;
    std::ofstream(host) << "found";
    auto directory = factory->CreatePhysicalFileSystem(temp);
    std::string content;
    EXPECT_TRUE(ReadWholeFile(*directory, "/" + name, content)) << temp;
    EXPECT_EQ(content, "found");
    std::error_code ec;
    std::filesystem::remove(host, ec);
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

TEST(FactoryTest, CreateBuiltinCommandsKnowsEveryBuiltin) {
    auto factory = Factory::Create();
    auto builtins = factory->CreateBuiltinCommands();
    ASSERT_NE(builtins, nullptr);
    for (const char* name : {"cat", "echo", "ls", "mkdir", "pwd"}) {
        EXPECT_FALSE(builtins->GetBuiltinVersion(name).empty()) << name;
    }
    EXPECT_NE(factory->CreateBuiltinConfigurator(), nullptr);
}
