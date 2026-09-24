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
        // Tagging happens here, in the one place that knows the source, rather
        // than in the console -- a shared console has no business knowing who
        // its writers are, and only one kind of writer wants a label at all.
        m_physicalConsole->Write("[" + m_sourceName + "] " + message);
    }
}

}
