#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Haisos {

// A single LLM conversation, running on its own thread. Deliberately an
// observe-and-talk-to handle only: an agent is not stopped or killed through
// here. Whatever owns an agent's lifetime owns it outright -- a top-level agent
// is stopped through the IProcess wrapping it, and a subagent lives and dies
// with the agent that started it.
class IAgent {
public:
    virtual ~IAgent() = default;
    virtual void Post(const std::string& command) = 0;
    virtual void Send(const std::string& command) = 0;
    virtual std::shared_ptr<IAgent> GetParent() const = 0;
    virtual std::string Name() const = 0;

    // Waits up to timeoutMs for this agent to finish, returning whether it has.
    // A timeout of 0 does not wait at all, so WaitToFinish(0) is how to ask
    // "has it finished?" without blocking. There is deliberately no untimed
    // form: an interactive agent (see IsInteractive) never finishes on its own,
    // so an unbounded wait on one would never return.
    virtual bool WaitToFinish(uint64_t timeoutMs) = 0;

    // The agents below this one. With onlyDirectChildren, only the agents this
    // agent started itself; otherwise the whole subtree beneath it, in
    // depth-first order. Agents that have since been destroyed are not listed
    // either way (a parent only holds weak references to its children).
    virtual std::vector<std::shared_ptr<IAgent>> GetChildren(bool onlyDirectChildren) const = 0;

    virtual nlohmann::json GetHistory() const = 0;
    virtual std::string GetConsoleOutput() const = 0;
    virtual std::string GetStartTime() const = 0;

    // How deep in the agent tree this agent sits: 0 for one with no parent, 1
    // for a subagent it started, and so on -- i.e. the number of parent hops up
    // to the top. It is what bounds subagent recursion (agent_start refuses to
    // go beyond a fixed depth), so an agent tree cannot grow without end.
    virtual int GetDepth() const = 0;

    // Whether this agent keeps running after the job it was given is done,
    // waiting for the next prompt -- the way a coding agent's main agent does.
    // A non-interactive one finishes as soon as it has answered, which is what
    // a delegated, one-shot subagent wants.
    virtual bool IsInteractive() const = 0;

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

    virtual std::shared_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent> callerAgent = nullptr) = 0;
    // Whether this factory knows the given tool name. Meant to be cheap (no
    // allocation): it is called on every tool invocation, unlike
    // GetAvailableTools(), which builds a fresh list of names.
    virtual bool HasTool(const std::string& name) const = 0;
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
    // OS's tools, like reading files or starting other processes).
    virtual std::shared_ptr<IAgent> CreateAgent(
        const std::vector<std::string>& systemPrompts,
        const std::string& name,
        std::shared_ptr<IAgent> parent,
        std::shared_ptr<IAgentConsole> console,
        const std::string& startTime = "",
        bool interactive = true,
        std::shared_ptr<IToolFactory> additionalTools = nullptr) = 0;

    // The agent-management tool set (agent_start, agent_query, ...), shared by
    // every agent this service creates.
    virtual std::shared_ptr<IToolFactory> GetToolFactory() = 0;

    // A standalone, in-memory IAgentConsole: writes are just accumulated, not
    // routed anywhere physical.
    virtual std::shared_ptr<IAgentConsole> CreateAgentConsole() = 0;
};

}
