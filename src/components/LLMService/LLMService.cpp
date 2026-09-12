#include "LLMService.h"
#include <algorithm>
#include "src/components/Agent/Agent.h"
#include "src/components/LLMCommunicator/LLMCommunicator.h"
#include "src/components/ToolFactory/ToolFactory.h"
#include "src/components/ToolFactory/CompositeToolFactory.h"
#include "src/components/Console/InMemoryAgentConsole.h"

namespace Haisos {

LLMService::LLMService(
    INetworkService& networkService,
    const std::string& endpoint,
    const std::string& modelName,
    const std::string& apiKey)
    : m_networkService(networkService)
    , m_endpoint(endpoint)
    , m_modelName(modelName)
    , m_apiKey(apiKey)
    , m_toolFactory(std::make_unique<ToolFactory>(*this))
{
}

LLMService::~LLMService() = default;

void LLMService::CleanupFinishedAgents() {
    m_agents.erase(
        std::remove_if(m_agents.begin(), m_agents.end(),
            [](const std::shared_ptr<IAgent>& agent) {
                return agent->IsFinished();
            }),
        m_agents.end());
}

std::shared_ptr<IAgent> LLMService::CreateAgent(
    const std::vector<std::string>& systemPrompts,
    const std::string& name,
    std::shared_ptr<IAgent> parent,
    std::unique_ptr<IAgentConsole> console,
    const std::string& startTime,
    bool longRunning,
    IToolFactory* additionalTools)
{
    auto httpClient = m_networkService.CreateHTTPClient();
    auto llmCommunicator = std::make_unique<LLMCommunicator>(std::move(httpClient), m_endpoint, m_modelName, m_apiKey, name);
    std::unique_ptr<IToolFactory> toolFactory = std::make_unique<ToolFactory>(*this);
    if (additionalTools) {
        toolFactory = std::make_unique<CompositeToolFactory>(*additionalTools, std::move(toolFactory));
    }

    auto agent = std::make_shared<Agent>(
        std::move(llmCommunicator),
        std::move(toolFactory),
        std::move(console),
        systemPrompts,
        name,
        parent,
        startTime,
        longRunning);

    if (parent) {
        parent->AddChild(agent);
    }

    std::lock_guard<std::mutex> lock(m_agentsMutex);
    CleanupFinishedAgents();
    m_agents.push_back(agent);
    return agent;
}

IToolFactory& LLMService::GetToolFactory() {
    return *m_toolFactory;
}

std::unique_ptr<IAgentConsole> LLMService::CreateAgentConsole() {
    return std::make_unique<InMemoryAgentConsole>();
}

}
