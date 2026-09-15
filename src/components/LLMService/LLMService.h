#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "interfaces/IServicesCreator.h"

namespace Haisos {

class LLMService : public ILLMService {
public:
    LLMService(
        INetworkService& networkService,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey);
    ~LLMService() override;

    std::shared_ptr<IAgent> CreateAgent(
        const std::vector<std::string>& systemPrompts,
        const std::string& name,
        std::shared_ptr<IAgent> parent,
        std::unique_ptr<IAgentConsole> console,
        const std::string& startTime = "",
        bool longRunning = true,
        IToolFactory* additionalTools = nullptr) override;

    IToolFactory& GetToolFactory() override;
    std::unique_ptr<IAgentConsole> CreateAgentConsole() override;

private:
    void CleanupFinishedAgents();

    INetworkService& m_networkService;
    std::string m_endpoint;
    std::string m_modelName;
    std::string m_apiKey;
    std::unique_ptr<IToolFactory> m_toolFactory;

    // Every agent this service creates is kept alive here: a parent only
    // holds a weak_ptr to its children (see Agent::AddChild), so nothing else
    // would otherwise keep e.g. an agent_start-created subagent alive once
    // the tool call that created it returns.
    std::vector<std::shared_ptr<IAgent>> m_agents;
    std::mutex m_agentsMutex;
};

}
