#include "Factory.h"
#include "src/components/Console/Console.h"
#include "src/components/Console/VirtualConsole.h"
#include "src/components/HTTPClient/HTTPClient.h"
#include "src/components/LLMCommunicator/LLMCommunicator.h"
#include "src/components/ToolFactory/ToolFactory.h"
#include "src/components/HaisosEngine/HaisosEngine.h"
#include "src/components/Agent/Agent.h"

namespace Haisos {

Factory::Factory() = default;
Factory::~Factory() = default;

std::unique_ptr<IConsole> Factory::CreateConsole(bool registerAsLogMessageReceiver) {
    return std::make_unique<Console>(registerAsLogMessageReceiver);
}

std::unique_ptr<IVirtualConsole> Factory::CreateVirtualConsole() {
    return std::make_unique<VirtualConsole>();
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

std::shared_ptr<IAgent> Factory::CreateAgent(
    std::unique_ptr<ILLMCommunicator> llmCommunicator,
    std::unique_ptr<IToolFactory> toolFactory,
    std::unique_ptr<IConsole> console,
    const std::vector<std::string>& systemPrompts,
    const std::string& name,
    const std::string& color,
    std::shared_ptr<IAgent> parent,
    std::shared_ptr<IVirtualConsole> virtualConsole,
    const std::string& startTime,
    const JsonSendReceiveCallbacks& callbacks)
{
    auto agent = std::make_shared<Agent>(std::move(llmCommunicator), std::move(toolFactory), std::move(console), systemPrompts, name, color, parent, std::move(virtualConsole), startTime, callbacks);
    if (parent) {
        parent->RegisterChild(agent);
    }
    return agent;
}

std::unique_ptr<IHaisosEngine> Factory::CreateHaisosEngine(IFactory& factory) {
    return std::make_unique<HaisosEngine>(factory);
}

std::unique_ptr<IFactory> CreateFactory() {
    return std::make_unique<Factory>();
}

}
