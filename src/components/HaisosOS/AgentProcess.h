#pragma once
#include <memory>
#include <string>
#include "interfaces/IProcess.h"

namespace Haisos {

// An IProcess whose runtime is an LLM agent.
class AgentProcess : public IProcess {
public:
    AgentProcess(uint64_t pid, uint64_t parentPid, const std::string& name, std::shared_ptr<IAgent> agent);
    ~AgentProcess() override;

    uint64_t GetPid() const override;
    uint64_t GetParentPid() const override;
    std::string Name() const override;

    bool IsFinished() const override;
    void WaitToFinish() override;
    bool WaitToFinish(uint64_t timeoutMs) override;
    bool Stop(unsigned timeoutMs) override;
    void Kill() override;

    std::shared_ptr<IAgent> AsAgent() const override;

private:
    uint64_t m_pid;
    uint64_t m_parentPid;
    std::string m_name;
    std::shared_ptr<IAgent> m_agent;
};

}
