#include <gtest/gtest.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "ServicesCreator.h"

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
    auto servicesCreator = CreateServicesCreator();

    auto networkService = servicesCreator->CreateNetworkService();
    ASSERT_NE(networkService, nullptr);

    auto httpClient = networkService->CreateHTTPClient();
    EXPECT_NE(httpClient, nullptr);
}

TEST(ServicesCreatorTest, CreateLLMServiceCreatesAgent) {
    auto servicesCreator = CreateServicesCreator();
    auto networkService = servicesCreator->CreateNetworkService();

    auto llmService = servicesCreator->CreateLLMService(networkService, "http://localhost:11434/api/chat", "llama3", "");
    ASSERT_NE(llmService, nullptr);

    auto agent = llmService->CreateAgent(
        "test_agent",
        /*parent=*/nullptr,
        llmService->CreateAgentConsole(),
        /*additionalTools=*/nullptr,
        {"You are a helpful AI assistant."});

    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->Name(), "test_agent");
    // Nothing stops the agent here: IAgent has no Stop(), and the LLM service
    // that created it stops and joins it when it goes out of scope.
}

TEST(ServicesCreatorTest, LLMServiceExposesToolFactory) {
    auto servicesCreator = CreateServicesCreator();
    auto networkService = servicesCreator->CreateNetworkService();

    auto llmService = servicesCreator->CreateLLMService(networkService, "http://localhost:11434/api/chat", "llama3", "");

    auto toolFactory = llmService->GetToolFactory();
    ASSERT_NE(toolFactory, nullptr);
    EXPECT_FALSE(toolFactory->GetAvailableTools().empty());
}

TEST(ServicesCreatorTest, CreateEmptyInMemFileSystemIsReadWrite) {
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
    auto servicesCreator = CreateServicesCreator();
    auto filesystemService = servicesCreator->CreateFileSystemService();

    auto inner = filesystemService->CreateEmptyInMemFileSystem();
    auto readOnly = filesystemService->CreateReadOnlyFileSystem(inner);
    ASSERT_NE(readOnly, nullptr);

    EXPECT_LT(readOnly->OpenFile("new.txt", kWriteCreateTruncate, 0), 0);
    EXPECT_LT(readOnly->CreateDirectory("sub", 0), 0);
}

TEST(ServicesCreatorTest, CreateSubFileSystemConfinesToBasePath) {
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

TEST(ServicesCreatorTest, ComposedFileSystemOverlaysAtMountPoint) {
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

    auto composed = filesystemService->CreateComposedFileSystem(main, "/data", mounted);
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

TEST(ServicesCreatorTest, CloneProducesAnIndependentServicesCreator) {
    auto servicesCreator = CreateServicesCreator();

    auto clone = servicesCreator->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), servicesCreator.get());

    // The clone is usable on its own, and outlives the original -- which is the
    // point: a sub-OS must not depend on its parent's services creator.
    servicesCreator.reset();
    EXPECT_NE(clone->CreateFileSystemService(), nullptr);
    EXPECT_NE(clone->CreateNetworkService(), nullptr);
}

// --- Shutting the LLM service down ---
//
// ~LLMService used to be defaulted: it destroyed its lock first, then its
// agents one by one, each ~Agent waiting for its thread -- while the agents
// further down the list were still running, and could call agent_start, i.e.
// CreateAgent, on the lock and the list being destroyed. Now it refuses new
// agents from the moment it begins, stops every agent, and waits for each
// before letting go of any.

namespace {

// An HTTP client standing in for an LLM: its first answer asks for the tool
// it was made with, every later one is plain text.
class ToolCallingHTTPClient : public IHTTPClient {
public:
    explicit ToolCallingHTTPClient(std::string toolName) : m_toolName(std::move(toolName)) {}

    HTTPResponse Get(const std::string&) override { return HTTPResponse{404, "", "not an LLM endpoint"}; }
    HTTPResponse Post(const std::string& url, const std::string& body) override { return Post(url, body, {}); }
    HTTPResponse Post(const std::string&, const std::string&, const std::vector<HTTPHeader>&) override {
        if (m_asked) {
            return HTTPResponse{200, R"({"message": {"role": "assistant", "content": "done"}, "done": true})", ""};
        }
        m_asked = true;
        return HTTPResponse{200, R"({"message": {"role": "assistant", "content": "", "tool_calls": [{"function": {"name": ")" +
            m_toolName + R"(", "arguments": {}}}]}, "done": true})", ""};
    }

private:
    const std::string m_toolName;
    bool m_asked = false;
};

class ToolCallingNetworkService : public INetworkService {
public:
    explicit ToolCallingNetworkService(std::string toolName) : m_toolName(std::move(toolName)) {}
    std::shared_ptr<IHTTPClient> CreateHTTPClient() override { return std::make_shared<ToolCallingHTTPClient>(m_toolName); }

private:
    const std::string m_toolName;
};

// What the "spawn" tool, the test, and the thread destroying the service share.
struct SpawnGate {
    std::mutex mutex;
    std::condition_variable cv;
    bool inTool = false;
    bool released = false;
    bool called = false;
    bool refused = false;
};

// A tool that, once released, asks the LLM service for an agent -- as
// agent_start does -- and records whether it got one.
class SpawnToolFactory : public IToolFactory {
public:
    SpawnToolFactory(ILLMService* service, std::shared_ptr<SpawnGate> gate) : m_service(service), m_gate(std::move(gate)) {}

    std::shared_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent>) override {
        return name == "spawn" ? std::make_shared<SpawnTool>(m_service, m_gate) : nullptr;
    }
    bool HasTool(const std::string& name) const override { return name == "spawn"; }
    std::vector<std::string> GetAvailableTools() const override { return {"spawn"}; }
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override {
        return {{"spawn", "asks the LLM service for an agent", nlohmann::json::object()}};
    }

private:
    class SpawnTool : public ITool {
    public:
        SpawnTool(ILLMService* service, std::shared_ptr<SpawnGate> gate) : m_service(service), m_gate(std::move(gate)) {}
        ToolResult Call(std::shared_ptr<IAgent>, const nlohmann::json&) override {
            {
                std::unique_lock<std::mutex> lock(m_gate->mutex);
                m_gate->inTool = true;
                m_gate->cv.notify_all();
                // Bounded, so a test that fails before releasing cannot hang.
                m_gate->cv.wait_for(lock, std::chrono::seconds(10), [this] { return m_gate->released; });
            }
            auto agent = m_service->CreateAgent("late", nullptr, nullptr, nullptr, {"You are a helpful AI assistant."});
            std::lock_guard<std::mutex> lock(m_gate->mutex);
            m_gate->called = true;
            m_gate->refused = (agent == nullptr);
            return ToolResult{agent ? "created" : "refused", agent == nullptr};
        }
        nlohmann::json GetParametersSchema() const override { return nlohmann::json::object(); }

    private:
        ILLMService* m_service;
        std::shared_ptr<SpawnGate> m_gate;
    };

    ILLMService* m_service;
    std::shared_ptr<SpawnGate> m_gate;
};

} // namespace

// An agent someone else still holds is stopped and waited for all the same:
// it must not outlive the service, still running with tools that reach back
// into it.
TEST(ServicesCreatorTest, DestroyingTheLLMServiceStopsEveryAgentItCreated) {
    auto llmService = CreateServicesCreator()->CreateLLMService(
        std::make_shared<ToolCallingNetworkService>("unused"), "http://localhost:9999/api/chat", "llama3", "");
    // Interactive and given nothing to do: it would wait for commands forever.
    auto agent = llmService->CreateAgent("kept", nullptr, nullptr, nullptr, {"You are a helpful AI assistant."}, /*isInteractive=*/true);
    ASSERT_NE(agent, nullptr);
    EXPECT_FALSE(agent->WaitToFinish(0));

    llmService.reset();

    EXPECT_TRUE(agent->WaitToFinish(0));
}

// An agent busy in a tool call when the shutdown begins can still reach the
// service. What it asks of it then is refused, cleanly: the service's lock
// and list are still there, and so is the service, which waits for the
// agent's call to end.
TEST(ServicesCreatorTest, NoAgentIsCreatedOnceTheLLMServiceIsShuttingDown) {
    auto gate = std::make_shared<SpawnGate>();
    std::shared_ptr<ILLMService> llmService = CreateServicesCreator()->CreateLLMService(
        std::make_shared<ToolCallingNetworkService>("spawn"), "http://localhost:9999/api/chat", "llama3", "");
    ILLMService* service = llmService.get();
    auto busy = llmService->CreateAgent("busy", nullptr, nullptr, std::make_shared<SpawnToolFactory>(service, gate),
        {"You are a helpful AI assistant."}, /*isInteractive=*/false);
    // Interactive and idle: it finishes only once the shutdown stops it, which
    // is after new agents have been refused.
    auto idle = llmService->CreateAgent("idle", nullptr, nullptr, nullptr, {"You are a helpful AI assistant."}, /*isInteractive=*/true);
    ASSERT_NE(busy, nullptr);
    ASSERT_NE(idle, nullptr);

    busy->Post("spawn an agent");
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        ASSERT_TRUE(gate->cv.wait_for(lock, std::chrono::seconds(10), [&] { return gate->inTool; }));
    }

    // Destroyed on a thread of its own: its destructor waits for the busy
    // agent, whose tool call this test releases.
    std::thread destroyer([s = std::move(llmService)]() mutable { s.reset(); });
    const bool shutdownBegan = idle->WaitToFinish(10000);
    {
        std::lock_guard<std::mutex> lock(gate->mutex);
        gate->released = true;
    }
    gate->cv.notify_all();
    destroyer.join();

    EXPECT_TRUE(shutdownBegan);
    EXPECT_TRUE(busy->WaitToFinish(0));
    std::lock_guard<std::mutex> lock(gate->mutex);
    EXPECT_TRUE(gate->called);
    EXPECT_TRUE(gate->refused);
}
