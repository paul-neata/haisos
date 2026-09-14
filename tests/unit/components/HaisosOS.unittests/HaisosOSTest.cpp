#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "HaisosOS.h"
#include "Factory.h"
#include "ServicesCreator.h"

using namespace Haisos;

namespace {

const std::string kTestRoot = "/tmp/haisos_os_test_root";
const std::string kUnreachableEndpoint = "http://localhost:9999/api/chat";

class HaisosOSTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(kTestRoot);
        std::filesystem::create_directories(kTestRoot + "/sub");
        std::ofstream(kTestRoot + "/hello.md") << "Say hello.";
        std::ofstream(kTestRoot + "/sub/inner.md") << "Say hi from inside sub.";
        std::ofstream(kTestRoot + "/not_a_program.txt") << "irrelevant";
        std::ofstream(kTestRoot + "/script.lua") << "os_list_directory({})";
    }

    void TearDown() override {
        std::filesystem::remove_all(kTestRoot);
    }

    OSEnvironment TestEnvironment() const {
        return OSEnvironment{
            {kEnvEndpoint, kUnreachableEndpoint},
            {kEnvModel, "llama3"},
        };
    }

    std::shared_ptr<IHaisosOS> BuildOS() {
        std::shared_ptr<IServicesCreator> servicesCreator = m_factory.CreateServicesCreator();
        std::shared_ptr<IFileSystem> rootFileSystem = m_factory.CreatePhysicalFileSystem(kTestRoot);
        auto physicalConsole = m_factory.CreatePhysicalConsole(false);
        return m_factory.CreateHaisosOS(
            std::move(servicesCreator), physicalConsole, rootFileSystem, TestEnvironment(), 0);
    }

    Factory m_factory;
};

TEST_F(HaisosOSTest, StartProcessAssignsIncrementingTopLevelPids) {
    auto os = BuildOS();

    auto p1 = os->StartProcess("hello.md", {}, nullptr);
    auto p2 = os->StartProcess("hello.md", {}, nullptr);

    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_NE(p1->GetPid(), p2->GetPid());
    EXPECT_EQ(p1->GetParentPid(), 0u);
    EXPECT_EQ(p2->GetParentPid(), 0u);
    EXPECT_NE(p1->AsAgent(), nullptr);
}

TEST_F(HaisosOSTest, StartProcessResolvesParentPidFromCallerAgent) {
    auto os = BuildOS();

    auto parent = os->StartProcess("hello.md", {}, nullptr);
    ASSERT_NE(parent, nullptr);

    auto child = os->StartProcess("hello.md", {}, parent->AsAgent());
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->GetParentPid(), parent->GetPid());
}

TEST_F(HaisosOSTest, StartProcessWithUnsupportedExtensionReturnsNull) {
    auto os = BuildOS();
    EXPECT_EQ(os->StartProcess("not_a_program.txt", {}, nullptr), nullptr);
}

TEST_F(HaisosOSTest, StartProcessRunsLuaScriptToCompletion) {
    auto os = BuildOS();
    auto process = os->StartProcess("script.lua", {}, nullptr);
    ASSERT_NE(process, nullptr);
    EXPECT_EQ(process->AsAgent(), nullptr);
    EXPECT_TRUE(process->WaitToFinish(2000));
    EXPECT_TRUE(process->IsFinished());
}

TEST_F(HaisosOSTest, StartProcessLuaMissingFileReturnsNull) {
    auto os = BuildOS();
    EXPECT_EQ(os->StartProcess("missing.lua", {}, nullptr), nullptr);
}

TEST_F(HaisosOSTest, GetRunningProcessesIncludesStartedProcess) {
    auto os = BuildOS();
    auto process = os->StartProcess("hello.md", {}, nullptr);
    ASSERT_NE(process, nullptr);

    auto running = os->GetRunningProcesses();
    bool found = false;
    for (const auto& p : running) {
        if (p->GetPid() == process->GetPid()) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(HaisosOSTest, CreateSubOSIsConfinedByTheRootItIsGiven) {
    // A sub-OS is confined by the filesystem handed to it, not by a permission
    // flag: narrowing the root to "sub" is what puts the parent's files
    // out of reach.
    auto os = BuildOS();
    auto subRoot = os->GetFileSystemService().CreateSubFileSystem(
        m_factory.CreatePhysicalFileSystem(kTestRoot), "sub");
    auto subOS = os->CreateSubOS(
        os->GetServicesCreator().Clone(),
        m_factory.CreatePhysicalConsole(false),
        subRoot,
        TestEnvironment(),
        /*osProcessId=*/7);
    ASSERT_NE(subOS, nullptr);

    EXPECT_NE(subOS->StartProcess("inner.md", {}, nullptr), nullptr);
    // hello.md lives in the parent root, not the sub-root.
    EXPECT_EQ(subOS->StartProcess("hello.md", {}, nullptr), nullptr);
}

TEST_F(HaisosOSTest, CreateSubOSCarriesTheCreatingProcessPid) {
    auto os = BuildOS();
    EXPECT_EQ(os->GetOSProcessID(), 0u);

    auto subOS = os->CreateSubOS(
        os->GetServicesCreator().Clone(),
        m_factory.CreatePhysicalConsole(false),
        m_factory.CreatePhysicalFileSystem(kTestRoot),
        TestEnvironment(),
        /*osProcessId=*/42);
    ASSERT_NE(subOS, nullptr);
    EXPECT_EQ(subOS->GetOSProcessID(), 42u);
}

TEST_F(HaisosOSTest, SubOSInheritsTheEnvironmentItIsGiven) {
    auto os = BuildOS();
    EXPECT_EQ(os->GetOsEnvironment().at(kEnvModel), "llama3");

    auto subOS = os->CreateSubOS(
        os->GetServicesCreator().Clone(),
        m_factory.CreatePhysicalConsole(false),
        m_factory.CreatePhysicalFileSystem(kTestRoot),
        os->GetOsEnvironment(),
        /*osProcessId=*/1);
    ASSERT_NE(subOS, nullptr);
    EXPECT_EQ(subOS->GetOsEnvironment().at(kEnvModel), "llama3");
}

} // namespace
