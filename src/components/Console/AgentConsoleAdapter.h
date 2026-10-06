#pragma once
#include <memory>
#include <string>
#include "interfaces/ILLMService.h"
#include "interfaces/IFactory.h"

namespace Haisos {

// Bridges a single agent's IAgentConsole onto a shared IPhysicalConsole: every
// write is one line, forward to the physical console as message + "\n",
// untagged; lines are read from it.
class AgentConsoleAdapter : public IAgentConsole {
public:
    static std::shared_ptr<AgentConsoleAdapter> Create(std::shared_ptr<IPhysicalConsole> physicalConsole) {
        return std::shared_ptr<AgentConsoleAdapter>(new AgentConsoleAdapter(std::move(physicalConsole)));
    }
    ~AgentConsoleAdapter() override;

    void Write(const std::string& message) override;
    std::optional<std::string> ReadLine() override;

private:
    explicit AgentConsoleAdapter(std::shared_ptr<IPhysicalConsole> physicalConsole);

    std::shared_ptr<IPhysicalConsole> m_physicalConsole;
};

}
