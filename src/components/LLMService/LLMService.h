#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "interfaces/IServicesCreator.h"
#include "src/components/Agent/Agent.h"

namespace Haisos {

class LLMService : public ILLMService {
public:
    static std::shared_ptr<LLMService> Create(
        std::shared_ptr<INetworkService> networkService,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey);
    ~LLMService() override;

    std::shared_ptr<IAgent> CreateAgent(
        const std::string& name,
        std::shared_ptr<IAgent> parent,
        std::shared_ptr<IAgentConsole> console,
        std::shared_ptr<IToolFactory> additionalTools,
        const std::vector<std::string>& systemPrompts,
        bool isInteractive = true,
        const std::string& startTime = "") override;

    std::shared_ptr<IToolFactory> GetToolFactory() override;
    std::shared_ptr<IAgentConsole> CreateAgentConsole() override;

private:
    LLMService(
        std::shared_ptr<INetworkService> networkService,
        const std::string& endpoint,
        const std::string& modelName,
        const std::string& apiKey);

    // Erases finished agents. Must be called with m_agentsMutex held.
    void CleanupFinishedAgents();

    std::shared_ptr<INetworkService> m_networkService;
    std::string m_endpoint;
    std::string m_modelName;
    std::string m_apiKey;
    std::shared_ptr<IToolFactory> m_toolFactory;

    // Declared before what it guards, so that it is destroyed after it: the
    // agents' own tools reach back into this service (agent_start), and
    // nothing may ever find the lock gone while the list is still there.
    std::mutex m_agentsMutex;
    // Set by the destructor, under m_agentsMutex, before it stops the agents:
    // from then on CreateAgent refuses, so no agent can be added to a list
    // that is being torn down.
    bool m_shuttingDown = false;
    // Every agent this service creates is kept alive here: a parent only
    // holds a weak_ptr to its children (see Agent::AddChild), so nothing else
    // would otherwise keep e.g. an agent_start-created subagent alive once
    // the tool call that created it returns. They are held as concrete Agents
    // rather than IAgents because stopping one is not part of IAgent: this
    // service owns their lifetime, so it is what has to be able to end it.
    std::vector<std::shared_ptr<Agent>> m_agents;
};

}
