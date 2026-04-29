#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "interfaces/IAgent.h"

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

    bool Stop(unsigned timeoutMs) override {
        m_stopped = true;
        m_stopTimeoutValue = timeoutMs;
        return true;
    }

    void Kill() override {
        m_killed = true;
        m_stopped = true;
    }

    std::shared_ptr<IAgent> GetParent() const override {
        return m_parent;
    }

    std::string Name() const override {
        return m_name;
    }

    void WaitToFinish() override {
        m_waited = true;
        m_finished = true;
    }

    bool WaitToFinish(uint64_t timeout_ms) override {
        m_waitedWithTimeout = true;
        m_waitTimeoutValue = timeout_ms;
        if (timeout_ms > 0) {
            m_waited = true;
        }
        return m_finished;
    }

    std::vector<std::shared_ptr<IAgent>> GetChildren() const override {
        std::vector<std::shared_ptr<IAgent>> result;
        for (const auto& wp : m_children) {
            if (auto sp = wp.lock()) {
                result.push_back(sp);
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

    bool IsFinished() const override {
        return m_finished;
    }

    bool IsKilled() const override {
        return m_killed;
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

    void AddChild(std::shared_ptr<IAgent> child) override {
        m_children.push_back(child);
    }

public:
    const std::vector<std::string>& GetCommands() const { return m_commands; }
    bool WasStopped() const { return m_stopped; }
    unsigned GetStopTimeoutValue() const { return m_stopTimeoutValue; }
    bool WasWaited() const { return m_waited; }
    bool WasWaitedWithTimeout() const { return m_waitedWithTimeout; }
    uint64_t GetWaitTimeoutValue() const { return m_waitTimeoutValue; }
    void SetName(const std::string& name) { m_name = name; }
    void SetParent(std::shared_ptr<IAgent> parent) { m_parent = parent; }
    void SetHistory(const nlohmann::json& history) { m_history = history; }
    void SetFinished(bool finished) { m_finished = finished; }
    void SetKilled(bool killed) { m_killed = killed; }
    void SetStartTime(const std::string& startTime) { m_startTime = startTime; }
    void SetConsoleOutput(const std::string& output) { m_consoleOutput = output; }

private:
    std::vector<std::string> m_commands;
    bool m_stopped = false;
    unsigned m_stopTimeoutValue = 0;
    bool m_waited = false;
    bool m_waitedWithTimeout = false;
    uint64_t m_waitTimeoutValue = 0;
    bool m_finished = false;
    bool m_killed = false;
    std::string m_name = "MockAgent";
    std::string m_startTime;
    std::string m_consoleOutput;
    std::shared_ptr<IAgent> m_parent;
    std::vector<std::weak_ptr<IAgent>> m_children;
    nlohmann::json m_history = nlohmann::json::array();
};

}
