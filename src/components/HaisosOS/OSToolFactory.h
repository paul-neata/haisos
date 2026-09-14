#pragma once
#include <functional>
#include <string>
#include <vector>
#include "interfaces/IHaisosOS.h"

namespace Haisos {

// The OS-level tool set (read/write files, list directories, start/list
// processes, ...) -- distinct from ILLMService's agent-management tool set.
// Merged with an agent's own tools via CompositeToolFactory when an
// agent-backed process is started by an IHaisosOS.
class OSToolFactory : public IToolFactory {
public:
    OSToolFactory(IHaisosOS& os);

    OSToolFactory(const OSToolFactory&) = delete;
    OSToolFactory& operator=(const OSToolFactory&) = delete;

    std::unique_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent> callerAgent = nullptr) override;
    bool HasTool(const std::string& name) const override;
    std::vector<std::string> GetAvailableTools() const override;
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override;

private:
    struct ToolEntry {
        std::string name;
        std::function<std::string()> getDescription;
        std::function<nlohmann::json()> getSchema;
        std::function<std::unique_ptr<ITool>()> create;
    };

    std::vector<ToolEntry> m_registry;
    IHaisosOS& m_os;
};

}
