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

    std::shared_ptr<IEnvironment> TestEnvironment() {
        auto environment = m_factory.CreateEnvironment();
        environment->SetVariable(kEnvEndpoint, kUnreachableEndpoint);
        environment->SetVariable(kEnvModel, "llama3");
        return environment;
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

TEST_F(HaisosOSTest, StartProcessAssignsUniqueTopLevelPids) {
    auto os = BuildOS();

    auto p1 = os->StartProcess(TestEnvironment(), "hello.md", {});
    auto p2 = os->StartProcess(TestEnvironment(), "hello.md", {});

    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_NE(p1->GetPid(), p2->GetPid());
    EXPECT_EQ(p1->GetParentPid(), 0u);
    EXPECT_EQ(p2->GetParentPid(), 0u);
    EXPECT_NE(p1->AsAgent(), nullptr);
}

TEST_F(HaisosOSTest, GetNextGloballyUniquePIDNeverRepeatsAcrossOSes) {
    auto os = BuildOS();
    auto other = BuildOS();

    uint64_t first = os->GetNextGloballyUniquePID();
    uint64_t second = other->GetNextGloballyUniquePID();
    uint64_t third = os->GetNextGloballyUniquePID();

    EXPECT_NE(first, 0u);
    EXPECT_NE(first, second);
    EXPECT_NE(second, third);
    EXPECT_NE(first, third);
}

TEST_F(HaisosOSTest, StartProcessKeepsTheEnvironmentItWasGiven) {
    auto os = BuildOS();
    auto environment = TestEnvironment();
    environment->SetVariable("GREETING", "hello");

    auto process = os->StartProcess(environment, "hello.md", {});
    ASSERT_NE(process, nullptr);
    ASSERT_NE(process->GetEnvironment(), nullptr);
    EXPECT_EQ(process->GetEnvironment()->GetVariable("GREETING").value_or(""), "hello");
    // The OS's own environment is untouched: nothing is shared implicitly.
    EXPECT_FALSE(os->GetOsEnvironment()->HasVariable("GREETING"));
}

TEST_F(HaisosOSTest, StartProcessWithoutAnEnvironmentReturnsNull) {
    auto os = BuildOS();
    EXPECT_EQ(os->StartProcess(nullptr, "hello.md", {}), nullptr);
}

TEST_F(HaisosOSTest, StartProcessWithUnsupportedExtensionReturnsNull) {
    auto os = BuildOS();
    EXPECT_EQ(os->StartProcess(TestEnvironment(), "not_a_program.txt", {}), nullptr);
}

TEST_F(HaisosOSTest, StartProcessRunsLuaScriptToCompletion) {
    auto os = BuildOS();
    auto process = os->StartProcess(TestEnvironment(), "script.lua", {});
    ASSERT_NE(process, nullptr);
    EXPECT_EQ(process->AsAgent(), nullptr);
    EXPECT_TRUE(process->WaitToFinish(2000));
    EXPECT_TRUE(process->IsFinished());
}

TEST_F(HaisosOSTest, StartProcessLuaMissingFileReturnsNull) {
    auto os = BuildOS();
    EXPECT_EQ(os->StartProcess(TestEnvironment(), "missing.lua", {}), nullptr);
}

TEST_F(HaisosOSTest, GetRunningProcessesIncludesStartedProcess) {
    auto os = BuildOS();
    auto process = os->StartProcess(TestEnvironment(), "hello.md", {});
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
    auto filesystemService = os->GetServicesCreator()->CreateFileSystemService();
    auto subRoot = filesystemService->CreateSubFileSystem(
        m_factory.CreatePhysicalFileSystem(kTestRoot), "sub");
    auto subOS = os->CreateSubOS(
        os->GetServicesCreator()->Clone(),
        m_factory.CreatePhysicalConsole(false),
        subRoot,
        os->GetOsEnvironment()->Clone(),
        /*osProcessId=*/7);
    ASSERT_NE(subOS, nullptr);

    EXPECT_NE(subOS->StartProcess(TestEnvironment(), "inner.md", {}), nullptr);
    // hello.md lives in the parent root, not the sub-root.
    EXPECT_EQ(subOS->StartProcess(TestEnvironment(), "hello.md", {}), nullptr);
}

TEST_F(HaisosOSTest, CreateSubOSCarriesTheCreatingProcessPid) {
    auto os = BuildOS();
    EXPECT_EQ(os->GetOSProcessID(), 0u);

    auto subOS = os->CreateSubOS(
        os->GetServicesCreator()->Clone(),
        m_factory.CreatePhysicalConsole(false),
        m_factory.CreatePhysicalFileSystem(kTestRoot),
        os->GetOsEnvironment()->Clone(),
        /*osProcessId=*/42);
    ASSERT_NE(subOS, nullptr);
    EXPECT_EQ(subOS->GetOSProcessID(), 42u);
}

TEST_F(HaisosOSTest, SubOSGetsACopyOfTheEnvironmentItIsGiven) {
    auto os = BuildOS();
    EXPECT_EQ(os->GetOsEnvironment()->GetVariable(kEnvModel).value_or(""), "llama3");

    auto subOS = os->CreateSubOS(
        os->GetServicesCreator()->Clone(),
        m_factory.CreatePhysicalConsole(false),
        m_factory.CreatePhysicalFileSystem(kTestRoot),
        os->GetOsEnvironment()->Clone(),
        /*osProcessId=*/1);
    ASSERT_NE(subOS, nullptr);
    EXPECT_EQ(subOS->GetOsEnvironment()->GetVariable(kEnvModel).value_or(""), "llama3");

    // A clone, not a share: what the sub-OS sets stays in the sub-OS.
    subOS->GetOsEnvironment()->SetVariable(kEnvModel, "other-model");
    EXPECT_EQ(os->GetOsEnvironment()->GetVariable(kEnvModel).value_or(""), "llama3");
}

TEST_F(HaisosOSTest, CreateHaisosOSWithoutAnEnvironmentReturnsNull) {
    std::shared_ptr<IServicesCreator> servicesCreator = m_factory.CreateServicesCreator();
    std::shared_ptr<IFileSystem> rootFileSystem = m_factory.CreatePhysicalFileSystem(kTestRoot);
    auto physicalConsole = m_factory.CreatePhysicalConsole(false);
    EXPECT_EQ(
        m_factory.CreateHaisosOS(std::move(servicesCreator), physicalConsole, rootFileSystem, nullptr, 0),
        nullptr);
}

} // namespace
