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
#include "interfaces/IVirtualConsole.h"
#include "interfaces/JsonSendReceiveCallbacks.h"
#include "src/components/libheaders/SynchronizedQueueEx.h"

namespace Haisos {

class Agent : public IAgent, public std::enable_shared_from_this<Agent> {
public:
    Agent(
        std::shared_ptr<ILLMCommunicator> llmCommunicator,
        std::shared_ptr<IToolFactory> toolFactory,
        std::shared_ptr<IConsole> console,
        const std::vector<std::string>& systemPrompts,
        const std::string& name,
        const std::string& color,
        std::shared_ptr<IAgent> parent,
        std::shared_ptr<IVirtualConsole> virtualConsole = nullptr,
        const std::string& startTime = "",
        const JsonSendReceiveCallbacks& callbacks = {});

    ~Agent() override;

    // IAgent interface
    void Post(const std::string& command) override;
    void Send(const std::string& command) override;
    void Stop() override;
    void Kill() override;
    std::shared_ptr<IAgent> GetParent() const override;
    std::string Name() const override;
    std::string Color() const override;
    void WaitToFinish() override;
    bool WaitToFinish(uint64_t timeout_ms) override;
    std::vector<std::shared_ptr<IAgent>> GetChildren() const override;
    nlohmann::json GetHistory() const override;
    bool IsFinished() const override;
    bool IsKilled() const override;
    std::string GetStartTime() const override;
    std::shared_ptr<IVirtualConsole> GetVirtualConsole() const override;
    int GetDepth() const override;

protected:
    void AddChild(std::shared_ptr<IAgent> child) override;

private:
    void RunThread();
    std::vector<std::tuple<std::string, std::string, std::string>> ExecuteToolCalls(const LLMMessage& message);

    std::shared_ptr<ILLMCommunicator> m_llmCommunicator;
    std::shared_ptr<IToolFactory> m_toolFactory;
    std::shared_ptr<IConsole> m_console;
    std::shared_ptr<IVirtualConsole> m_virtualConsole;
    std::vector<std::string> m_systemPrompts;
    std::string m_name;
    std::string m_color;
    std::string m_startTime;
    std::shared_ptr<IAgent> m_parent;
    JsonSendReceiveCallbacks m_callbacks;
    mutable std::mutex m_historyMutex;
    std::vector<LLMMessage> m_history;

    mutable std::mutex m_childrenMutex;
    std::vector<std::weak_ptr<IAgent>> m_children;

    SynchronizedQueueEx<std::string> m_commandQueue;
    std::thread m_thread;
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_killed{false};
    std::condition_variable m_finishedCv;
    std::mutex m_finishedMutex;
};

}
