#pragma once
#include <memory>
#include "interfaces/ILLMService.h"

namespace Haisos {

// Merges two IToolFactory instances into one, by tool name. sharedFactory is a
// long-lived factory shared with others (e.g. an OS's tool set, shared across
// every process it starts) and is consulted first; ownedFactory is this
// instance's own (e.g. a fresh per-agent tool set).
class CompositeToolFactory : public IToolFactory {
public:
    static std::shared_ptr<CompositeToolFactory> Create(
        std::shared_ptr<IToolFactory> sharedFactory, std::shared_ptr<IToolFactory> ownedFactory) {
        return std::shared_ptr<CompositeToolFactory>(
            new CompositeToolFactory(std::move(sharedFactory), std::move(ownedFactory)));
    }
    ~CompositeToolFactory() override;

    std::shared_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent> callerAgent = nullptr) override;
    bool HasTool(const std::string& name) const override;
    std::vector<std::string> GetAvailableTools() const override;
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override;

private:
    CompositeToolFactory(std::shared_ptr<IToolFactory> sharedFactory, std::shared_ptr<IToolFactory> ownedFactory);

    std::shared_ptr<IToolFactory> m_sharedFactory;
    std::shared_ptr<IToolFactory> m_ownedFactory;
};

}
