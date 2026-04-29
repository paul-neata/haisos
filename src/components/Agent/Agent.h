#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "interfaces/IAgent.h"
#include "interfaces/ILLMCommunicator.h"
#include "interfaces/IToolFactory.h"
#include "interfaces/IConsole.h"
#include "interfaces/SystemCallbacks.h"
#include "src/components/libheaders/SynchronizedQueueEx.h"
#include "AgentMessageBuffer.h"

namespace Haisos {

class Agent : public IAgent, public std::enable_shared_from_this<Agent> {
public:
    Agent(
        std::shared_ptr<ILLMCommunicator> llmCommunicator,
        std::shared_ptr<IToolFactory> toolFactory,
        std::shared_ptr<IConsole> console,
        const std::vector<std::string>& systemPrompts,
        const std::string& name,
        std::shared_ptr<IAgent> parent,
        const std::string& startTime = "",
        const SystemCallbacks& callbacks = {},
        bool longRunning = true);

    ~Agent() override;

    // IAgent interface
    void Post(const std::string& command) override;
    void Send(const std::string& command) override;
    bool Stop(unsigned timeoutMs) override;
    void Kill() override;
    std::shared_ptr<IAgent> GetParent() const override;
    std::string Name() const override;
    void WaitToFinish() override;
    bool WaitToFinish(uint64_t timeout_ms) override;
    std::vector<std::shared_ptr<IAgent>> GetChildren() const override;
    nlohmann::json GetHistory() const override;
    std::string GetConsoleOutput() const override;
    bool IsFinished() const override;
    bool IsKilled() const override;
    std::string GetStartTime() const override;
    int GetDepth() const override;
    bool IsLongRunning() const override;

protected:
    void AddChild(std::shared_ptr<IAgent> child) override;

private:
    void RunThread();
    std::vector<std::tuple<std::string, std::string, std::string, bool>> ExecuteToolCalls(const LLMMessage& message);

    std::shared_ptr<ILLMCommunicator> m_llmCommunicator;
    std::shared_ptr<IToolFactory> m_toolFactory;
    std::shared_ptr<IConsole> m_console;
    std::vector<std::string> m_systemPrompts;
    std::string m_name;
    std::string m_startTime;
    std::shared_ptr<IAgent> m_parent;
    SystemCallbacks m_callbacks;
    bool m_longRunning;
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
