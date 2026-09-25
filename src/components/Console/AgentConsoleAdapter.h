#pragma once
#include <memory>
#include <string>
#include "interfaces/ILLMService.h"
#include "interfaces/IFactory.h"

namespace Haisos {

// Bridges a single agent's IAgentConsole onto a shared IPhysicalConsole: writes
// are tagged with the agent's name, and lines are read from the physical console.
class AgentConsoleAdapter : public IAgentConsole {
public:
    static std::shared_ptr<AgentConsoleAdapter> Create(std::shared_ptr<IPhysicalConsole> physicalConsole, const std::string& sourceName) {
        return std::shared_ptr<AgentConsoleAdapter>(new AgentConsoleAdapter(std::move(physicalConsole), sourceName));
    }
    ~AgentConsoleAdapter() override;

    void Write(const std::string& message) override;
    std::optional<std::string> ReadLine() override;

private:
    AgentConsoleAdapter(std::shared_ptr<IPhysicalConsole> physicalConsole, const std::string& sourceName);

    std::shared_ptr<IPhysicalConsole> m_physicalConsole;
    std::string m_sourceName;
};

}
