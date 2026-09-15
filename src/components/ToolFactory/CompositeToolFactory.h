#pragma once
#include <memory>
#include "interfaces/ILLMService.h"

namespace Haisos {

// Merges two IToolFactory instances into one, by tool name. sharedFactory is a
// long-lived factory this instance does not own (e.g. an OS's tool set, shared
// across every process it starts); ownedFactory is owned by this instance (e.g.
// a fresh per-agent tool set).
class CompositeToolFactory : public IToolFactory {
public:
    CompositeToolFactory(IToolFactory& sharedFactory, std::unique_ptr<IToolFactory> ownedFactory);
    ~CompositeToolFactory() override;

    std::unique_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent> callerAgent = nullptr) override;
    bool HasTool(const std::string& name) const override;
    std::vector<std::string> GetAvailableTools() const override;
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override;

private:
    IToolFactory& m_sharedFactory;
    std::unique_ptr<IToolFactory> m_ownedFactory;
};

}
