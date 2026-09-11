#include "AgentConsoleAdapter.h"

namespace Haisos {

AgentConsoleAdapter::AgentConsoleAdapter(std::shared_ptr<IPhysicalConsole> physicalConsole, const std::string& sourceName)
    : m_physicalConsole(std::move(physicalConsole))
    , m_sourceName(sourceName)
{
}

AgentConsoleAdapter::~AgentConsoleAdapter() = default;

void AgentConsoleAdapter::Write(const std::string& message) {
    if (m_physicalConsole) {
        m_physicalConsole->Write(m_sourceName, message);
    }
}

}
