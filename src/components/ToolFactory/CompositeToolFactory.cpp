#include "CompositeToolFactory.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

CompositeToolFactory::CompositeToolFactory(IToolFactory& sharedFactory, std::unique_ptr<IToolFactory> ownedFactory)
    : m_sharedFactory(sharedFactory)
    , m_ownedFactory(std::move(ownedFactory))
{
}

CompositeToolFactory::~CompositeToolFactory() = default;

std::unique_ptr<ITool> CompositeToolFactory::CreateTool(const std::string& name, std::shared_ptr<IAgent> callerAgent) {
    if (m_sharedFactory.HasTool(name)) {
        return m_sharedFactory.CreateTool(name, callerAgent);
    }
    if (m_ownedFactory && m_ownedFactory->HasTool(name)) {
        return m_ownedFactory->CreateTool(name, callerAgent);
    }
    LogWarning("CompositeToolFactory: Unknown tool requested: %s", name.c_str());
    return nullptr;
}

bool CompositeToolFactory::HasTool(const std::string& name) const {
    return (m_ownedFactory && m_ownedFactory->HasTool(name)) || m_sharedFactory.HasTool(name);
}

std::vector<std::string> CompositeToolFactory::GetAvailableTools() const {
    std::vector<std::string> result = m_sharedFactory.GetAvailableTools();
    if (m_ownedFactory) {
        auto ownedNames = m_ownedFactory->GetAvailableTools();
        result.insert(result.end(), ownedNames.begin(), ownedNames.end());
    }
    return result;
}

std::vector<std::tuple<std::string, std::string, nlohmann::json>> CompositeToolFactory::GetAvailableToolDescriptions() const {
    auto result = m_sharedFactory.GetAvailableToolDescriptions();
    if (m_ownedFactory) {
        auto ownedDescriptions = m_ownedFactory->GetAvailableToolDescriptions();
        result.insert(result.end(), ownedDescriptions.begin(), ownedDescriptions.end());
    }
    return result;
}

}
