#include "InMemoryAgentConsole.h"

namespace Haisos {

void InMemoryAgentConsole::Write(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_contents.append(message);
    m_contents.push_back('\n');
}

void InMemoryAgentConsole::WriteError(const std::string& message) {
    // In memory there is no second stream to keep diagnostics apart on.
    Write(message);
}

std::string InMemoryAgentConsole::GetContents() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_contents;
}

}
