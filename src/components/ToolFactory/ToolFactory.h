#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "interfaces/IToolFactory.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

class IFactory;

class ToolFactory : public IToolFactory {
public:
    ToolFactory();
    explicit ToolFactory(IFactory& factory);

    ToolFactory(const ToolFactory&) = delete;
    ToolFactory& operator=(const ToolFactory&) = delete;
    ToolFactory(ToolFactory&&) = delete;
    ToolFactory& operator=(ToolFactory&&) = delete;

    // IToolFactory interface
    std::unique_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent> callerAgent = nullptr) override;
    std::vector<std::string> GetAvailableTools() const override;
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override;

private:
    struct ToolEntry {
        std::string name;
        std::function<std::string()> getDescription;
        std::function<nlohmann::json()> getSchema;
        std::function<std::unique_ptr<ITool>(std::shared_ptr<IAgent>)> create;
    };

    std::vector<ToolEntry> m_registry;
    IFactory* m_factory = nullptr;
};

} // namespace Haisos
