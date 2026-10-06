#include "AgentConsoleAdapter.h"

namespace Haisos {

AgentConsoleAdapter::AgentConsoleAdapter(std::shared_ptr<IPhysicalConsole> physicalConsole)
    : m_physicalConsole(std::move(physicalConsole))
{
}

AgentConsoleAdapter::~AgentConsoleAdapter() = default;

void AgentConsoleAdapter::Write(const std::string& message) {
    if (m_physicalConsole) {
        // One Write per line: the physical console adds no newline of its own.
        m_physicalConsole->Write(message + "\n");
    }
}

std::optional<std::string> AgentConsoleAdapter::ReadLine() {
    // Input is not tagged: what the user types is addressed to whoever is
    // reading, and a label would only have to be stripped off again.
    return m_physicalConsole ? m_physicalConsole->ReadLine() : std::nullopt;
}

}
