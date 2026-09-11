#include "LLMService.h"
#include "src/components/ToolFactory/CompositeToolFactory.h"

namespace Haisos {

LLMService::LLMService(
    IFactory& factory,
    INetworkService& networkService,
    const std::string& endpoint,
    const std::string& modelName,
    const std::string& apiKey)
    : m_factory(factory)
    , m_networkService(networkService)
    , m_endpoint(endpoint)
    , m_modelName(modelName)
    , m_apiKey(apiKey)
    , m_toolFactory(factory.CreateToolFactory(factory))
{
}

LLMService::~LLMService() = default;

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
    auto llmCommunicator = m_factory.CreateLLMCommunicator(std::move(httpClient), m_endpoint, m_modelName, m_apiKey);
    std::unique_ptr<IToolFactory> toolFactory = m_factory.CreateToolFactory(m_factory);
    if (additionalTools) {
        toolFactory = std::make_unique<CompositeToolFactory>(*additionalTools, std::move(toolFactory));
    }

    return m_factory.CreateAgent(
        std::move(llmCommunicator),
        std::move(toolFactory),
        std::move(console),
        systemPrompts,
        name,
        parent,
        startTime,
        longRunning);
}

IToolFactory& LLMService::GetToolFactory() {
    return *m_toolFactory;
}

}
