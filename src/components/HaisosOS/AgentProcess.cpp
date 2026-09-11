#include "AgentProcess.h"

namespace Haisos {

AgentProcess::AgentProcess(uint64_t pid, uint64_t parentPid, const std::string& name, std::shared_ptr<IAgent> agent)
    : m_pid(pid)
    , m_parentPid(parentPid)
    , m_name(name)
    , m_agent(std::move(agent))
{
}

AgentProcess::~AgentProcess() = default;

uint64_t AgentProcess::GetPid() const {
    return m_pid;
}

uint64_t AgentProcess::GetParentPid() const {
    return m_parentPid;
}

std::string AgentProcess::Name() const {
    return m_name;
}

bool AgentProcess::IsFinished() const {
    return m_agent->IsFinished();
}

void AgentProcess::WaitToFinish() {
    m_agent->WaitToFinish();
}

bool AgentProcess::WaitToFinish(uint64_t timeoutMs) {
    return m_agent->WaitToFinish(timeoutMs);
}

bool AgentProcess::Stop(unsigned timeoutMs) {
    return m_agent->Stop(timeoutMs);
}

void AgentProcess::Kill() {
    m_agent->Kill();
}

std::shared_ptr<IAgent> AgentProcess::AsAgent() const {
    return m_agent;
}

}
