#pragma once
#include <string>
#include <vector>
#include "interfaces/IConsole.h"

namespace Haisos::Mocks {

class MockConsole : public IConsole {
public:
    MockConsole() = default;

    void Write(const std::string& message) override {
        m_messages.push_back(message);
    }

    void Write(const IAgent& agent, const std::string& message) override {
        m_messages.push_back("[" + agent.Name() + "] " + message);
    }

    void Start() override {}
    void Stop() override {}

    const std::vector<std::string>& GetMessages() const { return m_messages; }
    void Clear() { m_messages.clear(); }

private:
    std::vector<std::string> m_messages;
};

}
