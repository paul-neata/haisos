#pragma once
#include <string>
#include <vector>
#include "interfaces/ILLMService.h"

namespace Haisos::Mocks {

class MockAgentConsole : public IAgentConsole {
public:
    MockAgentConsole() = default;

    void Write(const std::string& message) override {
        m_messages.push_back(message);
    }

    const std::vector<std::string>& GetMessages() const { return m_messages; }
    void Clear() { m_messages.clear(); }

private:
    std::vector<std::string> m_messages;
};

}
