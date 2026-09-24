#include "InMemoryAgentConsole.h"

namespace Haisos {

void InMemoryAgentConsole::Write(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_contents.append(message);
    m_contents.push_back('\n');
}

std::string InMemoryAgentConsole::GetContents() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_contents;
}

}
