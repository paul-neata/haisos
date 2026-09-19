#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "interfaces/ILLMService.h"
#include "interfaces/ILLMCommunicator.h"
#include "src/components/libheaders/SynchronizedQueueEx.h"
#include "AgentMessageBuffer.h"

namespace Haisos {

class Agent : public IAgent, public std::enable_shared_from_this<Agent> {
public:
    static std::shared_ptr<Agent> Create(
        std::shared_ptr<ILLMCommunicator> llmCommunicator,
        std::shared_ptr<IToolFactory> toolFactory,
        std::shared_ptr<IAgentConsole> console,
        const std::vector<std::string>& systemPrompts,
        const std::string& name,
        std::shared_ptr<IAgent> parent,
        const std::string& startTime = "",
        bool interactive = true);

    ~Agent() override;

    // IAgent interface
    void Post(const std::string& command) override;
    void Send(const std::string& command) override;
    std::shared_ptr<IAgent> GetParent() const override;
    std::string Name() const override;
    bool WaitToFinish(uint64_t timeoutMs) override;
    std::vector<std::shared_ptr<IAgent>> GetChildren(bool onlyDirectChildren) const override;
    nlohmann::json GetHistory() const override;
    std::string GetConsoleOutput() const override;
    std::string GetStartTime() const override;
    int GetDepth() const override;
    bool IsInteractive() const override;
    void AddChild(std::shared_ptr<IAgent> child) override;

    // Lifetime control, deliberately outside IAgent: an agent is stopped by
    // whoever owns it (the IProcess wrapping it, or this class's own
    // destructor), never by another agent or a tool holding an IAgent handle.
    bool Stop(unsigned timeoutMs);
    void Kill();
    // Blocks until the thread has finished. Only safe for an agent that will
    // finish on its own or has been stopped -- an interactive one never does.
    void WaitToFinish();
    bool IsFinished() const;
    bool IsKilled() const;

private:
    Agent(
        std::shared_ptr<ILLMCommunicator> llmCommunicator,
        std::shared_ptr<IToolFactory> toolFactory,
        std::shared_ptr<IAgentConsole> console,
        const std::vector<std::string>& systemPrompts,
        const std::string& name,
        std::shared_ptr<IAgent> parent,
        const std::string& startTime,
        bool interactive);

    // Starts the conversation thread. Called by Create() once the shared_ptr
    // owning this agent exists, so the thread can safely shared_from_this()
    // when it hands itself to a tool.
    void Start();

    void RunThread();
    std::vector<std::tuple<std::string, std::string, std::string, bool>> ExecuteToolCalls(const LLMMessage& message);

    std::shared_ptr<ILLMCommunicator> m_llmCommunicator;
    std::shared_ptr<IToolFactory> m_toolFactory;
    // Tool descriptions are fetched once in the constructor and reused for every LLM round.
    // This assumes the tool factory's registry is immutable after the Agent is constructed;
    // tools registered later will not be visible to this agent.
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> m_cachedToolDescriptions;
    std::shared_ptr<IAgentConsole> m_console;
    std::vector<std::string> m_systemPrompts;
    std::string m_name;
    std::string m_startTime;
    std::shared_ptr<IAgent> m_parent;
    bool m_interactive;
    mutable std::mutex m_historyMutex;
    std::vector<LLMMessage> m_history;

    mutable std::mutex m_childrenMutex;
    std::vector<std::weak_ptr<IAgent>> m_children;

    AgentMessageBuffer m_messageBuffer;

    SynchronizedQueueEx<std::string> m_commandQueue;
    std::thread m_thread;
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_killed{false};
    std::condition_variable m_finishedCv;
    std::mutex m_finishedMutex;
    std::mutex m_joinMutex;
};

}
