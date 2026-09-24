#pragma once
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "interfaces/ILLMService.h"

namespace Haisos::Mocks {

// Records writes, and replays scripted input: ReadLine hands out the lines
// given to SetInputLines in order, then reports end of input. Locked, because
// an interactive agent's input is read on a thread of its own.
class MockAgentConsole : public IAgentConsole {
public:
    MockAgentConsole() = default;

    void Write(const std::string& message) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_messages.push_back(message);
    }

    std::optional<std::string> ReadLine() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_readLineCalls;
        if (m_inputLines.empty()) {
            return std::nullopt;
        }
        std::string line = std::move(m_inputLines.front());
        m_inputLines.pop_front();
        return line;
    }

    std::vector<std::string> GetMessages() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_messages;
    }
    void Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_messages.clear();
    }
    void SetInputLines(const std::vector<std::string>& lines) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inputLines.assign(lines.begin(), lines.end());
    }
    int GetReadLineCalls() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_readLineCalls;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_messages;
    std::deque<std::string> m_inputLines;
    int m_readLineCalls = 0;
};

}
