#include "AgentMessageBuffer.h"

namespace Haisos {

AgentMessageBuffer::AgentMessageBuffer()
    : m_capacity(INITIAL_CAPACITY)
{
    m_buffer.reserve(m_capacity);
}

void AgentMessageBuffer::Append(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t needed = m_buffer.size() + text.size();
    if (needed > m_capacity) {
        if (m_capacity < MAX_DOUBLE_CAPACITY) {
            m_capacity *= 2;
            if (m_capacity < needed) {
                m_capacity = needed;
            }
        } else {
            m_capacity += OVERFLOW_INCREMENT;
            while (m_capacity < needed) {
                m_capacity += OVERFLOW_INCREMENT;
            }
        }
        m_buffer.reserve(m_capacity);
    }
    m_buffer.append(text);
}

const std::string& AgentMessageBuffer::GetContents() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buffer;
}

void AgentMessageBuffer::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.clear();
    m_buffer.reserve(INITIAL_CAPACITY);
    m_capacity = INITIAL_CAPACITY;
}

}
