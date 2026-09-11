#pragma once
#include <memory>
#include <string>
#include "interfaces/ILLMService.h"
#include "interfaces/IFactory.h"

namespace Haisos {

// Bridges a single agent's IAgentConsole writes onto a shared IPhysicalConsole,
// tagging each write with the agent's name.
class AgentConsoleAdapter : public IAgentConsole {
public:
    AgentConsoleAdapter(std::shared_ptr<IPhysicalConsole> physicalConsole, const std::string& sourceName);
    ~AgentConsoleAdapter() override;

    void Write(const std::string& message) override;

private:
    std::shared_ptr<IPhysicalConsole> m_physicalConsole;
    std::string m_sourceName;
};

}
