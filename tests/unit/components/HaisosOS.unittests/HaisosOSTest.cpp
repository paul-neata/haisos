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
        auto environment = m_factory->CreateEnvironment();
        environment->SetVariable(kEnvEndpoint, kUnreachableEndpoint);
        environment->SetVariable(kEnvModel, "llama3");
        return environment;
    }

    std::shared_ptr<IHaisosOS> BuildOS() {
        std::shared_ptr<IServicesCreator> servicesCreator = m_factory->CreateServicesCreator();
        std::shared_ptr<IFileSystem> rootFileSystem = m_factory->CreatePhysicalFileSystem(kTestRoot);
        auto physicalConsole = m_factory->CreatePhysicalConsole(false);
        return m_factory->CreateHaisosOS(
            std::move(servicesCreator), physicalConsole, rootFileSystem, TestEnvironment());
    }

    std::shared_ptr<IFactory> m_factory = CreateFactory();
};

TEST_F(HaisosOSTest, StartProcessAssignsUniqueTopLevelPids) {
    auto os = BuildOS();

    auto p1 = os->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"");
    auto p2 = os->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"");

    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_NE(p1->GetPid(), p2->GetPid());
    EXPECT_EQ(p1->GetParentPid(), 0u);
    EXPECT_EQ(p2->GetParentPid(), 0u);
    EXPECT_EQ(p1->Path(), "hello.md");
    // An agent-backed process names the agent running it; AsAgent is reachable
    // only from inside the process (ICurrentProcess).
    EXPECT_FALSE(p1->StartingAgentName().empty());
}

TEST_F(HaisosOSTest, EveryOSGetsAPidOfItsOwn) {
    auto os = BuildOS();
    auto other = BuildOS();

    EXPECT_NE(os->GetOSProcessID(), 0u);
    EXPECT_NE(other->GetOSProcessID(), 0u);
    EXPECT_NE(os->GetOSProcessID(), other->GetOSProcessID());
}

TEST_F(HaisosOSTest, StartProcessKeepsTheEnvironmentItWasGiven) {
    auto os = BuildOS();
    auto environment = TestEnvironment();
    environment->SetVariable("GREETING", "hello");

    auto process = os->StartProcess(environment, "hello.md", {}, /*workingDirectory=*/"");
    ASSERT_NE(process, nullptr);
    ASSERT_NE(process->GetEnvironment(), nullptr);
    EXPECT_EQ(process->GetEnvironment()->GetVariable("GREETING").value_or(""), "hello");
    // The OS's own environment is untouched: nothing is shared implicitly.
    EXPECT_FALSE(os->GetOsEnvironment()->HasVariable("GREETING"));
}

TEST_F(HaisosOSTest, StartProcessWithoutAnEnvironmentReturnsNull) {
    auto os = BuildOS();
    EXPECT_EQ(os->StartProcess(nullptr, "hello.md", {}, /*workingDirectory=*/""), nullptr);
}

TEST_F(HaisosOSTest, StartProcessWithUnsupportedExtensionReturnsNull) {
    auto os = BuildOS();
    EXPECT_EQ(os->StartProcess(TestEnvironment(), "not_a_program.txt", {}, /*workingDirectory=*/""), nullptr);
}

TEST_F(HaisosOSTest, StartProcessRunsLuaScriptToCompletion) {
    auto os = BuildOS();
    auto process = os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"");
    ASSERT_NE(process, nullptr);
    // Not an agent, so no agent name.
    EXPECT_TRUE(process->StartingAgentName().empty());
    EXPECT_TRUE(process->WaitToFinish(2000));
    // WaitToFinish(0) does not wait; it just reports that it has finished.
    EXPECT_TRUE(process->WaitToFinish(0));
}

TEST_F(HaisosOSTest, StartProcessLuaMissingFileReturnsNull) {
    auto os = BuildOS();
    EXPECT_EQ(os->StartProcess(TestEnvironment(), "missing.lua", {}, /*workingDirectory=*/""), nullptr);
}

TEST_F(HaisosOSTest, GetRunningProcessesIncludesStartedProcess) {
    auto os = BuildOS();
    auto process = os->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/"");
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
        m_factory->CreatePhysicalFileSystem(kTestRoot), "sub");
    auto subOS = os->CreateSubOS(
        os->GetServicesCreator()->Clone(),
        m_factory->CreatePhysicalConsole(false),
        subRoot,
        os->GetOsEnvironment()->Clone());
    ASSERT_NE(subOS, nullptr);

    EXPECT_NE(subOS->StartProcess(TestEnvironment(), "inner.md", {}, /*workingDirectory=*/""), nullptr);
    // hello.md lives in the parent root, not the sub-root.
    EXPECT_EQ(subOS->StartProcess(TestEnvironment(), "hello.md", {}, /*workingDirectory=*/""), nullptr);
}

TEST_F(HaisosOSTest, CreateSubOSCarriesTheParentOSPid) {
    auto os = BuildOS();

    auto subOS = os->CreateSubOS(
        os->GetServicesCreator()->Clone(),
        m_factory->CreatePhysicalConsole(false),
        m_factory->CreatePhysicalFileSystem(kTestRoot),
        os->GetOsEnvironment()->Clone());
    ASSERT_NE(subOS, nullptr);
    // A sub-OS is the same OS seen through a narrower root, so it keeps the
    // pid rather than taking one of its own.
    EXPECT_EQ(subOS->GetOSProcessID(), os->GetOSProcessID());
}

TEST_F(HaisosOSTest, SubOSGetsACopyOfTheEnvironmentItIsGiven) {
    auto os = BuildOS();
    EXPECT_EQ(os->GetOsEnvironment()->GetVariable(kEnvModel).value_or(""), "llama3");

    auto subOS = os->CreateSubOS(
        os->GetServicesCreator()->Clone(),
        m_factory->CreatePhysicalConsole(false),
        m_factory->CreatePhysicalFileSystem(kTestRoot),
        os->GetOsEnvironment()->Clone());
    ASSERT_NE(subOS, nullptr);
    EXPECT_EQ(subOS->GetOsEnvironment()->GetVariable(kEnvModel).value_or(""), "llama3");

    // A clone, not a share: what the sub-OS sets stays in the sub-OS.
    subOS->GetOsEnvironment()->SetVariable(kEnvModel, "other-model");
    EXPECT_EQ(os->GetOsEnvironment()->GetVariable(kEnvModel).value_or(""), "llama3");
}

// The current directory belongs to a process, not to a filesystem: a process
// starts where StartProcess put it, moves on its own, and moving does not move
// any other process.
TEST_F(HaisosOSTest, ProcessStartsAtTheWorkingDirectoryItWasGiven) {
    auto os = BuildOS();

    auto atRoot = os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"");
    auto inSub = os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/"sub");
    ASSERT_NE(atRoot, nullptr);
    ASSERT_NE(inSub, nullptr);

    // Only the process itself sees its working directory, so ask it as itself.
    auto atRootInside = std::dynamic_pointer_cast<ICurrentProcess>(atRoot);
    auto inSubInside = std::dynamic_pointer_cast<ICurrentProcess>(inSub);
    ASSERT_NE(atRootInside, nullptr);
    ASSERT_NE(inSubInside, nullptr);
    EXPECT_EQ(atRootInside->GetCurrentDirectory(), "/");
    EXPECT_EQ(inSubInside->GetCurrentDirectory(), "/sub");
}

TEST_F(HaisosOSTest, ChangeDirectoryMovesOnlyThatProcess) {
    auto os = BuildOS();

    auto first = std::dynamic_pointer_cast<ICurrentProcess>(
        os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/""));
    auto second = std::dynamic_pointer_cast<ICurrentProcess>(
        os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/""));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    EXPECT_EQ(first->ChangeDirectory("sub"), 0);
    EXPECT_EQ(first->GetCurrentDirectory(), "/sub");
    // The other process has not moved: a filesystem holds no cwd for them to share.
    EXPECT_EQ(second->GetCurrentDirectory(), "/");

    EXPECT_EQ(first->ChangeDirectory(".."), 0);
    EXPECT_EQ(first->GetCurrentDirectory(), "/");
}

TEST_F(HaisosOSTest, ChangeDirectoryRejectsWhatIsNotADirectory) {
    auto os = BuildOS();
    auto process = std::dynamic_pointer_cast<ICurrentProcess>(
        os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/""));
    ASSERT_NE(process, nullptr);

    EXPECT_EQ(process->ChangeDirectory("hello.md"), -1);
    EXPECT_EQ(process->ChangeDirectory("nowhere"), -1);
    EXPECT_EQ(process->GetCurrentDirectory(), "/");
}

// Reading a process's environment from outside must never be a way to change
// what the process itself sees.
TEST_F(HaisosOSTest, GetEnvironmentHandsOutACloneNotTheProcessOwn) {
    auto os = BuildOS();
    auto environment = TestEnvironment();
    environment->SetVariable("GREETING", "hello");
    auto process = os->StartProcess(environment, "hello.md", {}, /*workingDirectory=*/"");
    ASSERT_NE(process, nullptr);

    auto handedOut = process->GetEnvironment();
    ASSERT_NE(handedOut, nullptr);
    EXPECT_EQ(handedOut->GetVariable("GREETING").value_or(""), "hello");

    handedOut->SetVariable("GREETING", "tampered");
    EXPECT_EQ(process->GetEnvironment()->GetVariable("GREETING").value_or(""), "hello");
}

// A process reaches everything outside itself through its OS, and only through
// ICurrentProcess::GetHaisosOS() -- nothing is handed an IHaisosOS directly.
// That is what will let one process be given a narrower OS than another.
TEST_F(HaisosOSTest, ProcessReachesItsOSThroughGetHaisosOS) {
    auto os = BuildOS();
    auto process = std::dynamic_pointer_cast<ICurrentProcess>(
        os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/""));
    ASSERT_NE(process, nullptr);

    auto reached = process->GetHaisosOS();
    ASSERT_NE(reached, nullptr);
    EXPECT_EQ(reached->GetOSProcessID(), os->GetOSProcessID());
}

// The reference back to the OS is weak on purpose: an OS owns its processes, so
// a strong one would be a cycle and the OS would never be destroyed.
TEST_F(HaisosOSTest, ProcessDoesNotKeepItsOSAlive) {
    std::shared_ptr<ICurrentProcess> process;
    std::weak_ptr<IHaisosOS> osWatch;
    {
        auto os = BuildOS();
        osWatch = os;
        process = std::dynamic_pointer_cast<ICurrentProcess>(
            os->StartProcess(TestEnvironment(), "script.lua", {}, /*workingDirectory=*/""));
        ASSERT_NE(process, nullptr);
        ASSERT_NE(process->GetHaisosOS(), nullptr);
    }

    EXPECT_TRUE(osWatch.expired());
    EXPECT_EQ(process->GetHaisosOS(), nullptr);
}

TEST_F(HaisosOSTest, CreateHaisosOSWithoutAnEnvironmentReturnsNull) {
    std::shared_ptr<IServicesCreator> servicesCreator = m_factory->CreateServicesCreator();
    std::shared_ptr<IFileSystem> rootFileSystem = m_factory->CreatePhysicalFileSystem(kTestRoot);
    auto physicalConsole = m_factory->CreatePhysicalConsole(false);
    EXPECT_EQ(
        m_factory->CreateHaisosOS(std::move(servicesCreator), physicalConsole, rootFileSystem, nullptr),
        nullptr);
}

} // namespace
