#include "Factory.h"
#include <algorithm>
#include "src/components/Console/Console.h"
#include "src/components/HTTPClient/HTTPClient.h"
#include "src/components/LLMCommunicator/LLMCommunicator.h"
#include "src/components/ToolFactory/ToolFactory.h"
#include "src/components/HaisosEngine/HaisosEngine.h"
#include "src/components/Agent/Agent.h"
#include "src/components/Filesystem/Filesystem.h"

namespace Haisos {

Factory::Factory() = default;
Factory::~Factory() = default;

std::unique_ptr<IConsole> Factory::CreateConsole(bool registerAsLogMessageReceiver) {
    return std::make_unique<Console>(registerAsLogMessageReceiver);
}

std::unique_ptr<IHTTPClient> Factory::CreateHTTPClient() {
    return ::Haisos::CreateHTTPClient();
}

std::unique_ptr<ILLMCommunicator> Factory::CreateLLMCommunicator(
    std::unique_ptr<IHTTPClient> httpClient,
    const std::string& endpoint,
    const std::string& modelName,
    const std::string& apiKey)
{
    return std::make_unique<LLMCommunicator>(std::move(httpClient), endpoint, modelName, apiKey);
}

std::unique_ptr<IToolFactory> Factory::CreateToolFactory(IFactory& factory) {
    return std::make_unique<ToolFactory>(factory);
}

void Factory::CleanupFinishedAgents() {
    m_agents.erase(
        std::remove_if(m_agents.begin(), m_agents.end(),
            [](const std::shared_ptr<IAgent>& agent) {
                return agent->IsFinished();
            }),
        m_agents.end());
}

std::shared_ptr<IAgent> Factory::CreateAgent(
    std::unique_ptr<ILLMCommunicator> llmCommunicator,
    std::unique_ptr<IToolFactory> toolFactory,
    std::unique_ptr<IConsole> console,
    const std::vector<std::string>& systemPrompts,
    const std::string& name,
    std::shared_ptr<IAgent> parent,
    const std::string& startTime,
    bool longRunning)
{
    auto agent = std::make_shared<Agent>(std::move(llmCommunicator), std::move(toolFactory), std::move(console), systemPrompts, name, parent, startTime, m_systemCallbacks, longRunning);
    if (parent) {
        parent->AddChild(agent);
    }
    std::lock_guard<std::mutex> lock(m_agentsMutex);
    CleanupFinishedAgents();
    m_agents.push_back(agent);
    return agent;
}

std::unique_ptr<IHaisosEngine> Factory::CreateHaisosEngine(IFactory& factory) {
    return std::make_unique<HaisosEngine>(factory);
}

std::unique_ptr<IFilesystem> Factory::CreateFilesystem() {
    return std::make_unique<Filesystem>();
}

std::unique_ptr<IFactory> CreateFactory() {
    return std::make_unique<Factory>();
}

}
