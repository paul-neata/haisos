#pragma once
#include <functional>
#include <memory>
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
    // os is held by reference, not shared: the OS owns this factory, so sharing
    // it back would close an ownership cycle.
    static std::shared_ptr<OSToolFactory> Create(IHaisosOS& os) {
        return std::shared_ptr<OSToolFactory>(new OSToolFactory(os));
    }

    OSToolFactory(const OSToolFactory&) = delete;
    OSToolFactory& operator=(const OSToolFactory&) = delete;

    std::shared_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent> callerAgent = nullptr) override;
    bool HasTool(const std::string& name) const override;
    std::vector<std::string> GetAvailableTools() const override;
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override;

private:
    explicit OSToolFactory(IHaisosOS& os);

    struct ToolEntry {
        std::string name;
        std::function<std::string()> getDescription;
        std::function<nlohmann::json()> getSchema;
        std::function<std::shared_ptr<ITool>()> create;
    };

    std::vector<ToolEntry> m_registry;
    IHaisosOS& m_os;
};

}
