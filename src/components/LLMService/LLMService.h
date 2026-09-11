#pragma once
#include <memory>
#include <string>
#include "interfaces/IServicesCreator.h"

namespace Haisos {

class LLMService : public ILLMService {
public:
    LLMService(
        IFactory& factory,
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

private:
    IFactory& m_factory;
    INetworkService& m_networkService;
    std::string m_endpoint;
    std::string m_modelName;
    std::string m_apiKey;
    std::unique_ptr<IToolFactory> m_toolFactory;
};

}
