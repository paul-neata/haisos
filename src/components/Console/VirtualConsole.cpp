#include "VirtualConsole.h"
#include "interfaces/IAgent.h"

namespace Haisos {

constexpr size_t MAX_BUFFER_SIZE = 1024 * 1024;

static void AppendToBuffer(std::stringstream& buffer, const std::string& text) {
    std::string current = buffer.str();
    size_t newSize = current.size() + text.size();
    if (newSize > MAX_BUFFER_SIZE) {
        if (text.size() >= MAX_BUFFER_SIZE) {
            buffer.str(text.substr(text.size() - MAX_BUFFER_SIZE));
        } else {
            size_t excess = newSize - MAX_BUFFER_SIZE;
            buffer.str(current.substr(excess) + text);
        }
        buffer.clear();
    } else {
        buffer << text;
    }
}

void VirtualConsole::Write(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    AppendToBuffer(m_buffer, message + "\n");
}

void VirtualConsole::Write(const IAgent& agent, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    AppendToBuffer(m_buffer, "[" + agent.Name() + "] " + message + "\n");
}

std::string VirtualConsole::GetContents() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buffer.str();
}

void VirtualConsole::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.str("");
    m_buffer.clear();
}

}
