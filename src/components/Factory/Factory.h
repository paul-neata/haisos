#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "interfaces/IFactory.h"

namespace Haisos {

class Factory : public IFactory {
public:
    Factory();
    ~Factory() override;

    // IFactory interface
    std::unique_ptr<IConsole> CreateConsole(bool registerAsLogMessageReceiver) override;
    std::unique_ptr<IHTTPClient> CreateHTTPClient() override;
    std::unique_ptr<ILLMCommunicator> CreateLLMCommunicator(
        std::unique_ptr<IHTTPClient> httpClient,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey) override;
    std::unique_ptr<IToolFactory> CreateToolFactory(IFactory& factory) override;
    std::shared_ptr<IAgent> CreateAgent(
        std::unique_ptr<ILLMCommunicator> llmCommunicator,
        std::unique_ptr<IToolFactory> toolFactory,
        std::unique_ptr<IConsole> console,
        const std::vector<std::string>& systemPrompts,
        const std::string& name,
        std::shared_ptr<IAgent> parent,
        const std::string& startTime = "",
        bool longRunning = true) override;
    std::unique_ptr<IHaisosEngine> CreateHaisosEngine(IFactory& factory) override;
    std::unique_ptr<IFileSystem> CreateFilesystem() override;

    SystemCallbacks GetSystemCallbacks() const override { return m_systemCallbacks; }
    void SetSystemCallbacks(const SystemCallbacks& callbacks) override { m_systemCallbacks = callbacks; }

private:
    void CleanupFinishedAgents();

    SystemCallbacks m_systemCallbacks;
    std::vector<std::shared_ptr<IAgent>> m_agents;
    std::mutex m_agentsMutex;
};

std::unique_ptr<IFactory> CreateFactory();

}
