#pragma once
#include <string>
#include <mutex>

namespace Haisos {

class AgentMessageBuffer {
public:
    AgentMessageBuffer();

    void Append(const std::string& text);
    const std::string& GetContents() const;
    void Clear();

private:
    mutable std::mutex m_mutex;
    std::string m_buffer;
    size_t m_capacity;

    static constexpr size_t INITIAL_CAPACITY = 128 * 1024;
    static constexpr size_t MAX_DOUBLE_CAPACITY = 1024 * 1024;
    static constexpr size_t OVERFLOW_INCREMENT = 512 * 1024;
};

}
