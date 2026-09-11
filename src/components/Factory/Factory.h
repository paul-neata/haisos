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
    std::shared_ptr<IPhysicalConsole> CreatePhysicalConsole(bool registerAsLogMessageReceiver) override;
    std::unique_ptr<IAgentConsole> CreateAgentConsole() override;
    std::unique_ptr<IAgentConsole> CreateAgentConsoleFromPhysical(
        std::shared_ptr<IPhysicalConsole> physicalConsole,
        const std::string& sourceName) override;
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
        std::unique_ptr<IAgentConsole> console,
        const std::vector<std::string>& systemPrompts,
        const std::string& name,
        std::shared_ptr<IAgent> parent,
        const std::string& startTime = "",
        bool longRunning = true) override;
    std::unique_ptr<IFileSystem> CreateFilesystem() override;
    std::unique_ptr<IFileSystem> CreatePhysicalFileSystem(const std::string& rootPath) override;

private:
    void CleanupFinishedAgents();

    std::vector<std::shared_ptr<IAgent>> m_agents;
    std::mutex m_agentsMutex;
};

std::unique_ptr<IFactory> CreateFactory();

}
