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

namespace {

// How long each pass of the destructor's wait for an agent lasts before it is
// reported as still running. Only the reporting interval -- as in ~Agent, the
// wait itself never gives up.
constexpr uint64_t SHUTDOWN_WAIT_INTERVAL_MS = 5000;

} // namespace

LLMService::~LLMService() {
    // Defaulted, this destroyed the lock first and then the agents one by one,
    // each ~Agent waiting for its thread -- while the agents further down the
    // list kept running, and could call agent_start, i.e. CreateAgent, on the
    // lock and the list being destroyed. So the agents are ended here, while
    // every member is still alive, and in an order that leaves nothing racing:
    //  1. under the lock, no agent may be created any more, and the list is
    //     taken out of the service;
    //  2. every agent is asked to stop -- all of them before any is waited
    //     for, so they all wind down at once rather than one after another;
    //  3. each is waited for, outside the lock: a finishing agent may still
    //     call CreateAgent, which must be able to take the lock to refuse;
    //  4. only then are they released.
    // Waiting (3) rather than only releasing matters: an agent someone else
    // still holds would otherwise outlive this service, still running, with
    // tools holding a reference to it.
    std::vector<std::shared_ptr<Agent>> agents;
    {
        std::lock_guard<std::mutex> lock(m_agentsMutex);
        m_shuttingDown = true;
        agents.swap(m_agents);
    }
    LogInfo("LLMService: shutting down, stopping %zu agent(s)", agents.size());
    for (const auto& agent : agents) {
        agent->TriggerStop();
    }
    for (const auto& agent : agents) {
        while (!agent->WaitToFinish(SHUTDOWN_WAIT_INTERVAL_MS)) {
            LogWarning("LLMService: agent '%s' has not stopped after %llums of shutting down, still waiting",
                agent->Name().c_str(), static_cast<unsigned long long>(SHUTDOWN_WAIT_INTERVAL_MS));
        }
    }
    agents.clear();
    LogDebug("LLMService: shut down");
}

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
    // Refused before anything is made: an agent created now would be added to
    // a list that is being torn down (see ~LLMService). A finishing agent
    // calling agent_start is the usual way to get here.
    {
        std::lock_guard<std::mutex> lock(m_agentsMutex);
        if (m_shuttingDown) {
            LogWarning("LLMService::CreateAgent: refusing to create agent '%s': the LLM service is shutting down", name.c_str());
            return nullptr;
        }
    }

    LogInfo("LLMService::CreateAgent: creating agent '%s' parent='%s' isInteractive=%d",
        name.c_str(),
        parent ? parent->Name().c_str() : "(none)",
        isInteractive ? 1 : 0);

    if (!m_networkService) {
        LogError("LLMService::CreateAgent: refusing to create agent '%s': no network service", name.c_str());
        return nullptr;
    }

    // The agent's path from the top of its agent tree, which its LLM traffic is
    // reported under: every ancestor's name, top-most first, then its own.
    std::vector<std::string> agentPath{name};
    for (auto ancestor = parent; ancestor; ancestor = ancestor->GetParent()) {
        agentPath.insert(agentPath.begin(), ancestor->Name());
    }

    auto httpClient = m_networkService->CreateHTTPClient();
    auto llmCommunicator = LLMCommunicator::Create(std::move(httpClient), m_endpoint, m_modelName, m_apiKey, std::move(agentPath));
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

    {
        std::lock_guard<std::mutex> lock(m_agentsMutex);
        // Checked again: the shutdown may have begun while the agent was being
        // made, and the list it would join has then already been taken away.
        // The new agent has been given nothing to do, so it stops at once, and
        // is let go of when this returns.
        if (m_shuttingDown) {
            LogWarning("LLMService::CreateAgent: dropping agent '%s': the LLM service began shutting down while it was being created",
                name.c_str());
            agent->TriggerStop();
            return nullptr;
        }
        CleanupFinishedAgents();
        m_agents.push_back(agent);
    }

    // Only once it is kept alive here: a parent holds its children weakly.
    if (parent) {
        parent->AddChild(agent);
    }
    return agent;
}

std::shared_ptr<IToolFactory> LLMService::GetToolFactory() {
    return m_toolFactory;
}

std::shared_ptr<IAgentConsole> LLMService::CreateAgentConsole() {
    return InMemoryAgentConsole::Create();
}

}
