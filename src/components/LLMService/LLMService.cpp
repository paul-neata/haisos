#include "LLMService.h"
#include <algorithm>
#include "src/components/LLMCommunicator/LLMCommunicator.h"
#include "src/components/ToolFactory/ToolFactory.h"
#include "src/components/ToolFactory/CompositeToolFactory.h"
#include "src/components/Console/InMemoryAgentConsole.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

std::shared_ptr<LLMService> LLMService::Create(
    std::shared_ptr<INetworkService> networkService,
    const std::string& endpoint,
    const std::string& modelName,
    const std::string& apiKey)
{
    return std::shared_ptr<LLMService>(
        new LLMService(std::move(networkService), endpoint, modelName, apiKey));
}

LLMService::LLMService(
    std::shared_ptr<INetworkService> networkService,
    const std::string& endpoint,
    const std::string& modelName,
    const std::string& apiKey)
    : m_networkService(std::move(networkService))
    , m_endpoint(endpoint)
    , m_modelName(modelName)
    , m_apiKey(apiKey)
    , m_toolFactory(ToolFactory::Create(*this))
{
}

LLMService::~LLMService() = default;

void LLMService::CleanupFinishedAgents() {
    size_t sizeBefore = m_agents.size();
    m_agents.erase(
        std::remove_if(m_agents.begin(), m_agents.end(),
            [](const std::shared_ptr<Agent>& agent) {
                return agent->IsFinished();
            }),
        m_agents.end());
    size_t removed = sizeBefore - m_agents.size();
    if (removed > 0) {
        LogDebug("LLMService: cleaned up %zu finished agent(s)", removed);
    }
}

std::shared_ptr<IAgent> LLMService::CreateAgent(
    const std::string& name,
    std::shared_ptr<IAgent> parent,
    std::shared_ptr<IAgentConsole> console,
    std::shared_ptr<IToolFactory> additionalTools,
    const std::vector<std::string>& systemPrompts,
    bool isInteractive,
    const std::string& startTime)
{
    LogInfo("LLMService::CreateAgent: creating agent '%s' parent='%s' isInteractive=%d",
        name.c_str(),
        parent ? parent->Name().c_str() : "(none)",
        isInteractive ? 1 : 0);

    if (!m_networkService) {
        LogError("LLMService::CreateAgent: refusing to create agent '%s': no network service", name.c_str());
        return nullptr;
    }

    auto httpClient = m_networkService->CreateHTTPClient();
    auto llmCommunicator = LLMCommunicator::Create(std::move(httpClient), m_endpoint, m_modelName, m_apiKey, name);
    std::shared_ptr<IToolFactory> toolFactory = ToolFactory::Create(*this);
    if (additionalTools) {
        toolFactory = CompositeToolFactory::Create(std::move(additionalTools), std::move(toolFactory));
    }

    auto agent = Agent::Create(
        std::move(llmCommunicator),
        std::move(toolFactory),
        std::move(console),
        systemPrompts,
        name,
        parent,
        startTime,
        isInteractive);

    if (parent) {
        parent->AddChild(agent);
    }

    std::lock_guard<std::mutex> lock(m_agentsMutex);
    CleanupFinishedAgents();
    m_agents.push_back(agent);
    return agent;
}

std::shared_ptr<IToolFactory> LLMService::GetToolFactory() {
    return m_toolFactory;
}

std::shared_ptr<IAgentConsole> LLMService::CreateAgentConsole() {
    return InMemoryAgentConsole::Create();
}

}
