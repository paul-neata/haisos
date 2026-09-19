#pragma once
#include <memory>
#include <string>
#include "interfaces/IProcess.h"
#include "src/components/Agent/Agent.h"

namespace Haisos {

// An IProcess whose runtime is an LLM agent. It holds the concrete Agent rather
// than an IAgent: stopping and killing are deliberately not part of IAgent, and
// a process is exactly the thing that has to be able to do both.
class AgentProcess : public IProcess {
public:
    static std::shared_ptr<AgentProcess> Create(
        uint64_t pid,
        uint64_t parentPid,
        std::shared_ptr<IEnvironment> environment,
        const std::string& name,
        std::shared_ptr<Agent> agent);
    ~AgentProcess() override;

    uint64_t GetPid() const override;
    uint64_t GetParentPid() const override;
    std::string Name() const override;
    std::shared_ptr<IEnvironment> GetEnvironment() const override;

    bool IsFinished() const override;
    void WaitToFinish() override;
    bool WaitToFinish(uint64_t timeoutMs) override;
    bool Stop(unsigned timeoutMs) override;
    void Kill() override;

    std::shared_ptr<IAgent> AsAgent() const override;

private:
    AgentProcess(
        uint64_t pid,
        uint64_t parentPid,
        std::shared_ptr<IEnvironment> environment,
        const std::string& name,
        std::shared_ptr<Agent> agent);

    uint64_t m_pid;
    uint64_t m_parentPid;
    std::shared_ptr<IEnvironment> m_environment;
    std::string m_name;
    std::shared_ptr<Agent> m_agent;
};

}
