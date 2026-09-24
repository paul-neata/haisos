#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "interfaces/ILLMService.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

class ToolFactory : public IToolFactory {
public:
    static std::shared_ptr<ToolFactory> Create() {
        return std::shared_ptr<ToolFactory>(new ToolFactory());
    }
    // llmService is what gives the agent_start tool a way to create subagents;
    // without one, agent_start is unavailable (matching how a missing caller
    // agent is handled). It is held by reference, not shared: the service owns
    // the factory, so sharing it back would close an ownership cycle.
    static std::shared_ptr<ToolFactory> Create(ILLMService& llmService) {
        return std::shared_ptr<ToolFactory>(new ToolFactory(llmService));
    }

    ToolFactory(const ToolFactory&) = delete;
    ToolFactory& operator=(const ToolFactory&) = delete;
    ToolFactory(ToolFactory&&) = delete;
    ToolFactory& operator=(ToolFactory&&) = delete;

    // IToolFactory interface
    std::shared_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent> callerAgent = nullptr) override;
    bool HasTool(const std::string& name) const override;
    std::vector<std::string> GetAvailableTools() const override;
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override;

private:
    ToolFactory();
    explicit ToolFactory(ILLMService& llmService);

    struct ToolEntry {
        std::string name;
        std::function<std::string()> getDescription;
        std::function<nlohmann::json()> getSchema;
        std::function<std::shared_ptr<ITool>(std::shared_ptr<IAgent>)> create;
    };

    std::vector<ToolEntry> m_registry;
    ILLMService* m_llmService = nullptr;
};

} // namespace Haisos
