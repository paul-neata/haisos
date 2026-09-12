#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Haisos {

class IAgent {
public:
    virtual ~IAgent() = default;
    virtual void Post(const std::string& command) = 0;
    virtual void Send(const std::string& command) = 0;
    virtual bool Stop(unsigned timeoutMs) = 0;
    virtual void Kill() = 0;
    virtual std::shared_ptr<IAgent> GetParent() const = 0;
    virtual std::string Name() const = 0;
    virtual void WaitToFinish() = 0;
    virtual bool WaitToFinish(uint64_t timeoutMs) = 0;
    virtual std::vector<std::shared_ptr<IAgent>> GetChildren() const = 0;
    virtual nlohmann::json GetHistory() const = 0;
    virtual std::string GetConsoleOutput() const = 0;
    virtual bool IsFinished() const = 0;
    virtual bool IsKilled() const = 0;
    virtual std::string GetStartTime() const = 0;
    virtual int GetDepth() const = 0;
    virtual bool IsLongRunning() const = 0;
    virtual void AddChild(std::shared_ptr<IAgent> child) = 0;
};

struct ToolResult {
    std::string content;
    bool isError = false;
};

class ITool {
public:
    virtual ~ITool() = default;
    virtual ToolResult Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) = 0;
    virtual nlohmann::json GetParametersSchema() const = 0;
};

class IToolFactory {
public:
    virtual ~IToolFactory() = default;

    virtual std::unique_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent> callerAgent = nullptr) = 0;
    virtual std::vector<std::string> GetAvailableTools() const = 0;
    virtual std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const = 0;
};

// A single agent's write-only console. Deliberately minimal: an agent only ever
// needs to append its own output. Whether that ends up on the real console, in
// memory, or nowhere is decided by whoever creates it (see IFactory).
class IAgentConsole {
public:
    virtual ~IAgentConsole() = default;
    virtual void Write(const std::string& message) = 0;
};

class ILLMService {
public:
    virtual ~ILLMService() = default;

    // additionalTools, when non-null, is merged with this service's own
    // agent-management tools (e.g. so an OS-started process also gets the
    // OS's tools, like reading files or starting other processes). The
    // caller retains ownership; it must outlive the returned agent.
    virtual std::shared_ptr<IAgent> CreateAgent(
        const std::vector<std::string>& systemPrompts,
        const std::string& name,
        std::shared_ptr<IAgent> parent,
        std::unique_ptr<IAgentConsole> console,
        const std::string& startTime = "",
        bool longRunning = true,
        IToolFactory* additionalTools = nullptr) = 0;

    // The agent-management tool set (agent_start, agent_stop, ...), shared by
    // every agent this service creates.
    virtual IToolFactory& GetToolFactory() = 0;

    // A standalone, in-memory IAgentConsole: writes are just accumulated, not
    // routed anywhere physical.
    virtual std::unique_ptr<IAgentConsole> CreateAgentConsole() = 0;
};

}
