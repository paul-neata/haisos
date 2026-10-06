#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "interfaces/ILLMService.h"

namespace Haisos::Mocks {

// Records what was written, replies (Write) and diagnostics (WriteError) kept
// apart. Locked, because an agent writes on a thread of its own.
class MockAgentConsole : public IAgentConsole {
public:
    MockAgentConsole() = default;

    void Write(const std::string& message) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_messages.push_back(message);
    }

    void WriteError(const std::string& message) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_errors.push_back(message);
    }

    std::vector<std::string> GetMessages() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_messages;
    }
    std::vector<std::string> GetErrors() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_errors;
    }
    void Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_messages.clear();
        m_errors.clear();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_messages;
    std::vector<std::string> m_errors;
};

}
