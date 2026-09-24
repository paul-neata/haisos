#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "interfaces/ILLMService.h"

namespace Haisos::Mocks {

class MockAgent : public IAgent {
public:
    MockAgent() = default;

    void Post(const std::string& command) override {
        m_commands.push_back(command);
    }

    void Send(const std::string& command) override {
        m_commands.push_back(command);
    }

    std::shared_ptr<IAgent> GetParent() const override {
        return m_parent;
    }

    std::string Name() const override {
        return m_name;
    }

    void TriggerStop() override {
        m_stopTriggered = true;
    }

    bool WaitToFinish(uint64_t timeoutMs) override {
        m_waitedWithTimeout = true;
        m_waitTimeoutValue = timeoutMs;
        return m_finished;
    }

    std::vector<std::shared_ptr<IAgent>> GetChildren(bool onlyDirectChildren) const override {
        std::vector<std::shared_ptr<IAgent>> result;
        for (const auto& wp : m_children) {
            auto sp = wp.lock();
            if (!sp) {
                continue;
            }
            result.push_back(sp);
            if (!onlyDirectChildren) {
                auto descendants = sp->GetChildren(/*onlyDirectChildren=*/false);
                result.insert(result.end(), descendants.begin(), descendants.end());
            }
        }
        return result;
    }

    nlohmann::json GetHistory() const override {
        return m_history;
    }

    std::string GetConsoleOutput() const override {
        return m_consoleOutput;
    }

    std::string GetStartTime() const override {
        return m_startTime;
    }

    int GetDepth() const override {
        int depth = 0;
        auto p = GetParent();
        while (p) {
            ++depth;
            p = p->GetParent();
        }
        return depth;
    }

    bool IsInteractive() const override {
        return m_interactive;
    }

    // IAgent::AddChild is protected so that only LLMService may register a
    // child; a test double widens it again, since a test builds the hierarchy
    // by hand rather than going through a real LLMService.
    void AddChild(std::shared_ptr<IAgent> child) override {
        m_children.push_back(child);
    }

    const std::vector<std::string>& GetCommands() const { return m_commands; }
    bool WasStopTriggered() const { return m_stopTriggered; }
    bool WasWaitedWithTimeout() const { return m_waitedWithTimeout; }
    uint64_t GetWaitTimeoutValue() const { return m_waitTimeoutValue; }
    void SetName(const std::string& name) { m_name = name; }
    void SetParent(std::shared_ptr<IAgent> parent) { m_parent = parent; }
    void SetHistory(const nlohmann::json& history) { m_history = history; }
    void SetFinished(bool finished) { m_finished = finished; }
    void SetStartTime(const std::string& startTime) { m_startTime = startTime; }
    void SetConsoleOutput(const std::string& output) { m_consoleOutput = output; }
    void SetInteractive(bool interactive) { m_interactive = interactive; }

private:
    std::vector<std::string> m_commands;
    bool m_stopTriggered = false;
    bool m_waitedWithTimeout = false;
    uint64_t m_waitTimeoutValue = 0;
    bool m_finished = false;
    std::string m_name = "MockAgent";
    std::string m_startTime;
    std::string m_consoleOutput;
    std::shared_ptr<IAgent> m_parent;
    std::vector<std::weak_ptr<IAgent>> m_children;
    nlohmann::json m_history = nlohmann::json::array();
    bool m_interactive = false;
};

}
