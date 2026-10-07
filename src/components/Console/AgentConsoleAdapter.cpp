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

void AgentConsoleAdapter::WriteError(const std::string& message) {
    if (m_physicalConsole) {
        // A diagnostic goes to the host's stderr, like a reply goes to stdout.
        m_physicalConsole->WriteError(message + "\n");
    }
}

}
