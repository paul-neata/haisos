#pragma once
#include <memory>
#include <string>
#include "interfaces/ILLMService.h"
#include "interfaces/IFactory.h"

namespace Haisos {

// Bridges a single agent's IAgentConsole onto a shared IPhysicalConsole: every
// write is one line, forwarded to the physical console as message + "\n",
// untagged -- Write to its output, WriteError to its error. For agents running
// without an OS (the integration tests); a process's agent talks to the
// process's own descriptors instead (see ProcessAgentConsole in HaisosOS).
class AgentConsoleAdapter : public IAgentConsole {
public:
    static std::shared_ptr<AgentConsoleAdapter> Create(std::shared_ptr<IPhysicalConsole> physicalConsole) {
        return std::shared_ptr<AgentConsoleAdapter>(new AgentConsoleAdapter(std::move(physicalConsole)));
    }
    ~AgentConsoleAdapter() override;

    void Write(const std::string& message) override;
    void WriteError(const std::string& message) override;

private:
    explicit AgentConsoleAdapter(std::shared_ptr<IPhysicalConsole> physicalConsole);

    std::shared_ptr<IPhysicalConsole> m_physicalConsole;
};

}
